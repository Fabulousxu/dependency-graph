import cudf
import cugraph
from basic_types import SymbolTable
from dataclasses import dataclass


class DependencyGraph:
    @dataclass
    class PackageNode:
        id: int
        name: str
        versions: list['DependencyGraph.VersionNode']

    @dataclass
    class VersionNode:
        id: int
        version: str
        architecture: int
        dependencies: list['DependencyGraph.DependencyEdge']

    @dataclass
    class DependencyEdge:
        id: int
        from_version: 'DependencyGraph.VersionNode'
        to_package: 'DependencyGraph.PackageNode'
        version_constraint: str
        architecture_constraint: int
        dependency_type: int
        group: int

    def __init__(self):
        self.dependency_types = SymbolTable(
            ['Depends', 'Pre-Depends', 'Recommends', 'Suggests', 'Breaks', 'Conflicts', 'Provides', 'Replaces',
             'Enhances'])
        self.architectures = SymbolTable(['native', 'any', 'all'])
        self.packages: list[DependencyGraph.PackageNode] = []
        self.versions: list[DependencyGraph.VersionNode] = []
        self.dependencies: list[DependencyGraph.DependencyEdge] = []
        self.name_to_package: dict[str, DependencyGraph.PackageNode] = {}
        self.edgesdf = cudf.DataFrame()
        self.cugraph = cugraph.Graph(directed=True)

    def package_count(self) -> int:
        return len(self.packages)

    def version_count(self) -> int:
        return len(self.versions)

    def dependency_count(self) -> int:
        return len(self.dependencies)

    def create_package(self, name: str) -> PackageNode:
        pnode = self.name_to_package.get(name)
        if pnode:
            return pnode
        pnode = DependencyGraph.PackageNode(len(self.packages), name, [])
        self.packages.append(pnode)
        self.name_to_package[name] = pnode
        return pnode

    def create_version(self, pnode: PackageNode, version: str, architecture: int) -> VersionNode:
        for vnode in pnode.versions:
            if vnode.version == version and vnode.architecture == architecture:
                return vnode
        vnode = DependencyGraph.VersionNode(len(self.versions), version, architecture, [])
        self.versions.append(vnode)
        pnode.versions.append(vnode)
        return vnode

    def create_dependency(self, vnode: VersionNode, pnode: PackageNode, version_constraint: str, arch_constraint: int,
                          dependency_type: int, group: int) -> DependencyEdge:
        dep = DependencyGraph.DependencyEdge(len(self.dependencies), vnode, pnode, version_constraint, arch_constraint,
                                             dependency_type, group)
        self.dependencies.append(dep)
        vnode.dependencies.append(dep)
        return dep

    def build_cugraph(self):
        src = []
        dst = []
        did = []
        gid = []
        for pnode in self.packages:
            for vnode in pnode.versions:
                src.append(self.version_count() + pnode.id)
                dst.append(vnode.id)
                did.append(-1)
                gid.append(-1)
                for dep in vnode.dependencies:
                    src.append(vnode.id)
                    dst.append(self.version_count() + dep.to_package.id)
                    did.append(dep.id)
                    gid.append(dep.group)
        self.edgesdf = cudf.DataFrame({'src': src, 'dst': dst, 'did': did, 'gid': gid})
        self.cugraph.from_cudf_edgelist(self.edgesdf, 'src', 'dst', renumber=False)

    def query_dependencies(self, name: str, version: str, arch: str, depth: int, use_gpu: bool):
        res = [{'direct_dependencies': [], 'or_dependencies': []} for _ in range(depth)]
        frontier: list[DependencyGraph.VersionNode] = []
        pnode = self.name_to_package.get(name)
        if pnode:
            for vnode in pnode.versions:
                if version and vnode.version != version:
                    continue
                if arch and self.architectures.symbols[vnode.architecture] != arch:
                    continue
                frontier.append(vnode)
        if len(frontier) == 0:
            return res

        if use_gpu:
            bfs_edgesdf = cugraph.bfs(self.cugraph, [vnode.id for vnode in frontier], depth * 2 - 1)
            bfs_edgesdf = bfs_edgesdf[(bfs_edgesdf['distance'] > 0) & (bfs_edgesdf['distance'] < depth * 2)]
            bfs_edgesdf = bfs_edgesdf.merge(self.edgesdf, left_on=['predecessor', 'vertex'], right_on=['src', 'dst'])
            bfs_edgesdf['level'] = bfs_edgesdf['distance'] // 2
            direct_edgesdf = bfs_edgesdf[bfs_edgesdf['gid'] == 0]
            or_edgesdf = bfs_edgesdf[bfs_edgesdf['gid'] > 0]
            group_edgesdf = or_edgesdf.groupby(['level', 'src', 'gid']).agg({'did': 'collect'}).reset_index()

            for edge in direct_edgesdf[['level', 'did']].to_pandas().itertuples(index=False):
                dep = self.dependencies[edge.did]
                item = {
                    'name': dep.to_package.name,
                    'dependency_type': self.dependency_types.symbols[dep.dependency_type],
                    'version_constraint': dep.version_constraint,
                    'architecture_constraint': self.architectures.symbols[dep.architecture_constraint]
                }
                res[edge.level]['direct_dependencies'].append(item)
            for edge in group_edgesdf[['level', 'did']].to_pandas().itertuples(index=False):
                items = []
                for did in edge.did:
                    dep = self.dependencies[did]
                    item = {
                        'name': dep.to_package.name,
                        'dependency_type': self.dependency_types.symbols[dep.dependency_type],
                        'version_constraint': dep.version_constraint,
                        'architecture_constraint': self.architectures.symbols[dep.architecture_constraint]
                    }
                    items.append(item)
                res[edge.level]['or_dependencies'].append(items)
            return res

        visited = set([vnode.id for vnode in frontier])
        for level in range(depth):
            next: list[DependencyGraph.VersionNode] = []
            direct_items = []
            or_items = []
            for vnode in frontier:
                group_items = []
                for dep in vnode.dependencies:
                    item = {
                        'name': dep.to_package.name,
                        'dependency_type': self.dependency_types.symbols[dep.dependency_type],
                        'version_constraint': dep.version_constraint,
                        'architecture_constraint': self.architectures.symbols[dep.architecture_constraint]
                    }
                    if dep.group > 0:
                        while len(group_items) < dep.group:
                            group_items.append([])
                        group_items[dep.group - 1].append(item)
                    else:
                        direct_items.append(item)

                    if (level + 1 < depth and self.dependency_types.symbols[dep.dependency_type] == 'Depends'
                            and dep.group == 0):
                        for next_vnode in dep.to_package.versions:
                            if next_vnode.id in visited:
                                continue
                            if self.architectures.symbols[dep.architecture_constraint] == 'native':
                                match = next_vnode.architecture == vnode.architecture \
                                        or self.architectures.symbols[next_vnode.architecture] == 'all'
                            elif self.architectures.symbols[dep.architecture_constraint] == 'any':
                                match = True
                            else:
                                match = next_vnode.architecture == dep.architecture_constraint
                            if match:
                                next.append(next_vnode)
                                visited.add(next_vnode.id)
                for item in group_items:
                    or_items.append(item)
            res[level] = {'direct_dependencies': direct_items, 'or_dependencies': or_items}
            frontier = next
        return res
