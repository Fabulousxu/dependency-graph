class SymbolTable:
    def __init__(self, symbols: list[str]):
        self.symbols: list[str] = []
        for symbol in symbols:
            self.insert(symbol)

    def id(self, symbol: str) -> int | None:
        for i in range(len(self.symbols)):
            if self.symbols[i] == symbol:
                return i
        return None

    def insert(self, symbol: str) -> int:
        for i in range(len(self.symbols)):
            if self.symbols[i] == symbol:
                return i
        self.symbols.append(symbol)
        return len(self.symbols) - 1
