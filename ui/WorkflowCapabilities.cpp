#include "WorkflowCapabilities.h"

namespace wa::llm {

QString capabilitySummary() {
    return QStringLiteral(
        "只可提出 WinAuto 1.0 标准流程草稿。可用能力：定义目标窗口条件（进程名、标题、PID、宽高），"
        "启动程序、激活窗口、点击控件、设置控件值、输入文本、按键、等待、等待/断言控件及 if/repeat/while "
        "流程控制。每个步骤必须使用现有流程字段和已定义窗口别名。不得生成脚本或其他任意代码；"
        "不支持 Excel 读写、数据导出或定时触发。目标不明确时返回澄清问题；任务含不支持能力时明确列出。");
}

}  // namespace wa::llm
