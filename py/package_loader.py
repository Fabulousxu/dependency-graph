import time
from dependency_graph import DependencyGraph
from debian import deb822


class PackageLoader:
    def __init__(self, graph: DependencyGraph):
        self.graph = graph

    def load_from_file(self, filename, verbose: bool = False) -> bool:
        try:
            with open(filename, 'r', encoding='utf-8') as file:
                if verbose:
                    print(f"Loading packages from file: {filename} ... ", end="", flush=True)
                raw_pkgs = file.read()
            if verbose:
                begin = time.time()
            self._load_raw_packages(raw_pkgs)
            if verbose:
                end = time.time()
                print(f"Done. ({end - begin:.3f} s)")
                print(f"Loaded {self.graph.package_count()} packages, {self.graph.version_count()} versions, "
                      f"{self.graph.dependency_count()} dependencies.")
            return True
        except Exception:
            if verbose:
                print(f"Failed to open file: {filename}")
            return False

    def _parse_dependency(self, raw_dep: str, group: int):
        version = ''
        lpar = raw_dep.find('(')
        if lpar != -1:
            rpar = raw_dep.rfind(')')
            if rpar != -1 and rpar > lpar:
                version = raw_dep[lpar + 1:rpar].strip()
        name_and_arch = raw_dep[:lpar] if lpar != -1 else raw_dep
        colon = name_and_arch.find(':')
        arch = name_and_arch[colon + 1:].strip() if colon != -1 else 'native'
        return {
            'name': name_and_arch[:colon].strip() if colon != -1 else name_and_arch.strip(),
            'version_constraint': version,
            'architecture_constraint': self.graph.architectures.insert(arch),
            'group': group
        }

    def _parse_dependencies(self, raw_deps: str, group: list[int]) -> list:
        result = []
        for and_ in raw_deps.split(','):
            or_s = and_.split('|')
            if len(or_s) > 1:
                for or_ in or_s:
                    result.append(self._parse_dependency(or_, group[0]))
                group[0] += 1
            else:
                result.append(self._parse_dependency(and_, 0))
        return result

    def _load_raw_packages(self, raw_pkgs: str):
        for raw_pkg in raw_pkgs.split('\n\n'):
            if raw_pkg.strip():
                kv = deb822.Deb822(raw_pkg)
                pnode = self.graph.create_package(kv.get('Package'))
                arch = self.graph.architectures.insert(kv.get('Architecture'))
                vnode = self.graph.create_version(pnode, kv.get('Version'), arch)
                group = [1]
                for dtype in self.graph.dependency_types.symbols:
                    kv_dtype = kv.get(dtype)
                    if kv_dtype:
                        dtid = self.graph.dependency_types.id(dtype)
                        for item in self._parse_dependencies(kv_dtype, group):
                            dpnode = self.graph.create_package(item['name'])
                            self.graph.create_dependency(vnode, dpnode, item['version_constraint'],
                                                         item['architecture_constraint'], dtid, item['group'])
