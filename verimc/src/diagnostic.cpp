#include "verimc/diagnostic.hpp"
#include <map>

namespace verimc {
namespace {
std::string diagnosticMessage(const std::string& code, const std::string& details) {
    static const std::map<std::string, std::string> messages = {
        {"ESyntax", "语法错误"},
        {"ELanguageVersion", "语言版本不受支持"},
        {"EName", "名称或参数错误"},
        {"EResourceMissing", "源文件不存在或无法读取"},
        {"EImport", "导入路径超出项目边界"},
        {"EImportCycle", "导入形成循环"},
        {"EElaborationLimit", "编译资源预算超限"},
        {"ETypeMismatch", "类型不匹配"},
        {"EConstantRequired", "此处必须是编译期常量"},
        {"ERequire", "编译期条件不成立"},
        {"EOperatorDomain", "此类型不支持该运算"},
        {"EWidthRange", "位宽或索引越界"},
        {"ELiteralRange", "常量超出允许范围"},
        {"ELevelConversion", "红石强度与数字信号不能隐式转换"},
        {"EDriveDirection", "信号读写方向错误"},
        {"EMultipleDriver", "信号存在重叠驱动"},
        {"EUndriven", "信号未完整驱动"},
        {"ECombinationalCycle", "组合逻辑存在依赖环"},
        {"ERecursiveDesign", "设计或函数存在递归"},
        {"ENextTarget", "next 目标不是本模块的完整寄存器"},
        {"ENextConflict", "寄存器存在冲突更新"},
        {"EUnownedRegister", "寄存器没有更新所属块"},
        {"EClockDomain", "时钟域不符合单时钟约束"},
        {"EModelMissing", "物理组件的逻辑模型尚未接入"},
        {"ECapability", "此能力尚未实现"},
        {"EArtifactVersion", "中间文件版本不受支持"},
        {"EArtifactInvalid", "逻辑中间文件无效"}};
    auto it = messages.find(code);
    return (it == messages.end() ? "编译失败" : it->second) + std::string("：") + details;
}
} // namespace
Diagnostic::Diagnostic(std::string c, const SourceSpan& s, std::string m)
    : std::runtime_error(diagnosticMessage(c, m)), code(std::move(c)), span(s) {}
} // namespace verimc
