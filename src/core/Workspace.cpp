#include "Workspace.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"

Workspace& Workspace::instance() {
    static Workspace w;
    return w;
}

ToolManager* Workspace::tools() {
    if (!m_tools) m_tools = new ToolManager(this);
    return m_tools;
}

void Workspace::setView(CanvasView* v) {
    if (m_view == v) return;
    m_view = v;
    emit viewChanged();
}

Document* Workspace::doc() const { return m_view ? m_view->document() : nullptr; }
