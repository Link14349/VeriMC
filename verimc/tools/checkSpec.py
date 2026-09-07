"""Check the written grammar/examples and documentation; not a VeriMC compiler.

Run with dependencies from specQaRequirements.txt. No type checker, HDL execution,
cell mapping, physical simulation, or game verification is performed here.
"""

import json
from pathlib import Path
import re
import sys

from lark import Lark, UnexpectedInput


def checkDocuments(projectRoot):
    linkCount = 0
    for filePath in sorted(projectRoot.rglob("*.md")):
        if any(part.startswith("build") or part in {".cache", "testResults"} for part in filePath.relative_to(projectRoot).parts):
            continue
        content = filePath.read_text(encoding="utf-8")
        if not content.endswith("\n") or content.count("```") % 2:
            raise ValueError(f"文档换行或代码块不完整：{filePath}")
        for lineNumber, line in enumerate(content.splitlines(), 1):
            if line != line.rstrip():
                raise ValueError(f"行尾空白：{filePath}:{lineNumber}")
        for target in re.findall(r"\]\(([^)]+)\)", content):
            if "://" in target or target.startswith("#"):
                continue
            if not (filePath.parent / target.split("#")[0]).exists():
                raise ValueError(f"本地链接失效：{filePath} -> {target}")
            linkCount += 1
    return linkCount


def checkExamples(projectRoot, parser):
    examplesRoot = projectRoot / "examples"
    cases = json.loads((examplesRoot / "exampleCases.json").read_text())["cases"]
    listed = [case["file"] for case in cases]
    actual = {str(path.relative_to(examplesRoot)) for path in examplesRoot.rglob("*.vmc")}
    if len(listed) != len(set(listed)) or set(listed) != actual:
        raise ValueError("示例清单有重复、缺失或不存在的文件")
    accepted = rejected = importCount = 0
    for case in cases:
        filePath = examplesRoot / case["file"]
        content = filePath.read_text(encoding="utf-8")
        try:
            tree = parser.parse(content)
        except UnexpectedInput as error:
            if case["syntax"] != "reject":
                raise ValueError(f"意外语法错误：{filePath}:{error.line}:{error.column}") from error
            rejected += 1
        else:
            if case["syntax"] != "accept":
                raise ValueError(f"文法错误地接受了反例：{filePath}")
            if any(tree.find_data("_ambig")):
                raise ValueError(f"示例存在多棵文法解析树：{filePath}")
            accepted += 1
        for target in re.findall(r'\bimport\s+"([^"]+)"\s+as\b', content):
            if not (filePath.parent / target).is_file():
                raise ValueError(f"导入示例文件缺失：{filePath} -> {target}")
            importCount += 1
    return accepted, rejected, importCount


def checkLexicalBoundaries(parser):
    def source(body):
        return 'language "0.1"; package lexicalChecks; ' + body

    accepted = [
        source("const count: nat = 0xF_F;"),
        source("const count: nat = 0b10_01;"),
        source("const count: nat = 1_024;"),
        source("const trueValue: bit = true;"),
        source("/* 注释 */ const count: nat = 3; // 行注释\n"),
        "\ufeff" + source("const count: nat = 3;"),
        source("module Empty {}"),
    ]
    rejected = [
        source("const count: nat = 01;"),
        source("const count: nat = 0x_G;"),
        source("const count: nat = 1__2;"),
        source("const count: nat = 1_;"),
        source("const count: nat = 1.5;"),
        source("const count: nat = 1e2;"),
        source("const _count: nat = 1;"),
        source("const 数量: nat = 1;"),
        source("const count: nat = 1;\ufeff"),
        source("module Bad(width: nat = 4,) {}"),
        source("module Bad { input a: bits<4>; connect a = a[0:]; }"),
    ]
    for text in accepted:
        tree = parser.parse(text)
        if any(tree.find_data("_ambig")):
            raise ValueError("词法正例出现解析歧义")
    for text in rejected:
        try:
            parser.parse(text)
        except UnexpectedInput:
            continue
        raise ValueError(f"词法反例被接受：{text}")
    return len(accepted), len(rejected)


def checkIndependentMath():
    # Compare the proposed ripple-adder equations with integer addition.
    # This does not read or execute .vmc files.
    pairs = 0
    for left in range(16):
        for right in range(16):
            carry = 0
            sumValue = 0
            for index in range(4):
                a = (left >> index) & 1
                b = (right >> index) & 1
                sumValue |= (a ^ b ^ carry) << index
                carry = (a & b) | ((a ^ b) & carry)
            if sumValue != (left + right) % 16 or carry != (left + right) // 16:
                raise ValueError("全加器方程与整数加法不一致")
            pairs += 1
    state = 0
    for _ in range(15):
        state = (state + 1) % 16
    if state != 15 or (state + 1) % 16 != 0:
        raise ValueError("计数器预期序列不一致")
    oldLeft, oldRight = 1, 2
    nextLeft, nextRight = oldRight, oldLeft
    if (nextLeft, nextRight) != (2, 1):
        raise ValueError("同时更新预期不一致")
    return pairs


def main():
    projectRoot = Path(__file__).resolve().parents[1]
    parser = Lark((projectRoot / "grammar/verimcGrammar.lark").read_text(),
                  parser="earley", lexer="dynamic", ambiguity="explicit")
    accepted, rejected, imports = checkExamples(projectRoot, parser)
    lexicalAccepted, lexicalRejected = checkLexicalBoundaries(parser)
    links = checkDocuments(projectRoot)
    pairs = checkIndependentMath()
    print(f"文法示例：接受 {accepted}，按预期拒绝 {rejected}；已接受样例未发现解析歧义。")
    print(f"词法边界：接受 {lexicalAccepted}，按预期拒绝 {lexicalRejected}。")
    print(f"文档：{links} 个本地链接；示例：{imports} 个文件导入。")
    print(f"独立数学核算：{pairs} 对四位加法输入，以及计数回绕/旧值交换预期。")
    print("未执行 VeriMC 静态语义、HDL 测试、物理构建或 Minecraft 差分。")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, UnexpectedInput) as error:
        print(f"规范检查失败：{error}", file=sys.stderr)
        sys.exit(1)
