#pragma once
// Dialogues déplaçables À L'INTÉRIEUR de la fenêtre principale.
//
// Au lieu d'une fenêtre système centrée (qui masque l'image et ne peut pas toujours être déplacée : Wayland, gestionnaires
// de fenêtres en mosaïque…), le dialogue est incorporé dans un cadre flottant enfant de la fenêtre principale, avec sa barre
// de titre glissable. La position est mémorisée par type de dialogue (QSettings) et recadrée si la fenêtre est redimensionnée.
// Pendant l'affichage, menus/panneaux/raccourcis sont inhibés ; le canevas reste navigable (main, molette, zoom).
#include <QDialog>
#include <QString>

class MovableDialog : public QDialog {
    Q_OBJECT
public:
    explicit MovableDialog(QWidget* parent = nullptr) : QDialog(parent) {}
    int exec() override;                                   // même API que QDialog, mais incorporé et déplaçable
    void setPositionKey(const QString& k) { m_key = k; }
    QString positionKey() const { return m_key.isEmpty() ? QString::fromLatin1(metaObject()->className()) : m_key; }
private:
    QString m_key;
};

// Exécute n'importe quel QDialog (y compris QColorDialog) dans un cadre déplaçable. Retourne dlg->result().
int execMovable(QDialog* dlg, const QString& positionKey);

class QDialogButtonBox;
void frenchButtons(QDialogButtonBox* bb);   // OK / Annuler / Réinitialiser…

namespace Dlg {
QColor pickColor(const QColor& initial, const QString& title, QWidget* parent = nullptr);   // QColor invalide si annulé
int getInt(QWidget* parent, const QString& title, const QString& label, int def, int min, int max, bool* ok);
QString getText(QWidget* parent, const QString& title, const QString& label, const QString& def, bool* ok);
}
