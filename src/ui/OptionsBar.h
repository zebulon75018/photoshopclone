#pragma once
#include <QToolBar>
#include <functional>
#include "core/Selection.h"

// Barre d'options contextuelle (sous les menus), reconstruite à chaque changement d'outil.
// Les widgets sont liés directement aux champs de ToolSettings via des pointeurs.
class OptionsBar : public QToolBar {
    Q_OBJECT
public:
    explicit OptionsBar(QWidget* parent = nullptr);
    void clearOptions();
    void addLabel(const QString& text);
    void addSpin(const QString& label, int* value, int min, int max, const QString& suffix = QString());
    void addCheck(const QString& label, bool* value);
    void addCombo(const QString& label, const QStringList& items, int* value);
    void addSelectionModes(Sel::Mode* mode);
    void addFontControls();
    void addButton(const QString& text, const std::function<void()>& fn);
    void addSeparatorLine() { addSeparator(); }
};
