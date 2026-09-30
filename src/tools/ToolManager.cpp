#include "ToolManager.h"
#include "Tools.h"

ToolManager::ToolManager(QObject* parent) : QObject(parent) {
    auto add = [&](const QString& key, std::initializer_list<Tool*> tools) {
        ToolGroup g; g.key = key;
        for (Tool* t : tools) { t->setParent(this); g.tools << t; }
        m_groups << g;
    };
    // Même organisation et mêmes raccourcis que Photoshop.
    add("V", {new MoveTool});
    add("M", {new MarqueeTool(false), new MarqueeTool(true), new SelectionTransformTool});
    add("L", {new LassoTool, new PolyLassoTool});
    add("W", {new WandTool, new SamTool});
    add("C", {new CropTool});
    add("I", {new EyedropperTool});
    add("B", {new PaintTool(PaintTool::Brush), new PaintTool(PaintTool::Pencil)});
    add("S", {new PaintTool(PaintTool::Clone)});
    add("E", {new PaintTool(PaintTool::Eraser)});
    add("G", {new GradientTool, new BucketTool});
    add("R", {new PaintTool(PaintTool::Blur), new PaintTool(PaintTool::Sharpen), new PaintTool(PaintTool::Smudge)});
    add("O", {new PaintTool(PaintTool::Dodge), new PaintTool(PaintTool::Burn)});
    add("T", {new TextTool});
    add("U", {new ShapeTool});
    add("H", {new HandTool});
    add("Z", {new ZoomTool});
    m_transform = new TransformTool;
    m_transform->setParent(this);
    connect(m_transform, &TransformTool::finished, this, &ToolManager::restorePrevious);
    select(m_groups[6].tools[0]);   // pinceau par défaut
}

Tool* ToolManager::byId(const QString& id) const {
    for (auto& g : m_groups) for (Tool* t : g.tools) if (t->id() == id) return t;
    return id == m_transform->id() ? m_transform : nullptr;
}
Tool* ToolManager::hand() const { return byId("hand"); }
Tool* ToolManager::zoomTool() const { return byId("zoom"); }

void ToolManager::select(Tool* t) {
    if (!t || t == m_current) return;
    if (m_current) m_current->deactivate();
    if (m_current != m_transform) m_previous = m_current;
    m_current = t;
    for (auto& g : m_groups) { int i = g.tools.indexOf(t); if (i >= 0) g.current = i; }
    t->activate();
    emit toolChanged(t);
}

void ToolManager::selectGroup(int g) { if (g >= 0 && g < m_groups.size()) select(m_groups[g].active()); }

void ToolManager::cycleGroup(int g) {
    if (g < 0 || g >= m_groups.size() || m_groups[g].tools.size() < 2) { selectGroup(g); return; }
    ToolGroup& grp = m_groups[g];
    bool inGroup = grp.tools.contains(m_current);
    if (inGroup) grp.current = (grp.current + 1) % grp.tools.size();
    emit groupsChanged();
    select(grp.active());
}

void ToolManager::restorePrevious() { if (m_previous) select(m_previous); }
