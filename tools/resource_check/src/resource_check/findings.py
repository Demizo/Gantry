from dataclasses import dataclass


@dataclass
class Finding:
    file: str
    line: int
    text: str

    def __str__(self) -> str:
        return self.text
