#pragma once
// Boîtes de dialogue Stable Diffusion : réglages, génération d'image (texte -> image) et inpainting sur la sélection.
//
// La génération s'exécute dans un fil séparé (Sd::Job) : pendant le calcul le dialogue reste vivant (barre de progression, journal,
// bouton Annuler) et l'image reste navigable (déplacement, zoom). Fermer le dialogue pendant un calcul l'annule proprement et attend
// la fin de l'étape en cours avant de se fermer. L'application du résultat au document est faite par l'appelant (Ops::sd…).
#include <QDialog>
#include <QPointer>
#include "MovableDialog.h"
#include "ai/SdBackend.h"
#include "ai/SdImaging.h"
#include "core/Document.h"

class QCheckBox; class QComboBox; class QFormLayout; class QLabel; class QListWidget; class QPlainTextEdit; class QProgressBar;
class QPushButton; class QSpinBox; class QDoubleSpinBox; class QToolButton; class QTimer; class QVBoxLayout; class QLineEdit;
class QGridLayout; class QTabWidget;
class SliderSpin; class QScrollArea;

// Fenêtre de diagnostic d'un fichier de modèle (Sd::diagnoseModelFile) : format détecté, cohérence structurelle,
// aperçu des tenseurs, interprétation d'architecture. Fenêtre indépendante (pas un MovableDialog) : simple popup
// d'information, à l'image des QMessageBox déjà utilisées ailleurs dans l'application.
class ModelDiagnosticDialog : public QDialog {
    Q_OBJECT
public:
    ModelDiagnosticDialog(const QString& path, QWidget* parent);
private:
    void refresh(bool withSha256);
    QString m_path;
    QPlainTextEdit* m_text;
    QPushButton* m_shaButton;
};

class SdSettingsDialog : public MovableDialog {
    Q_OBJECT
public:
    explicit SdSettingsDialog(QWidget* parent);
    void accept() override;
private:
    struct Row { QString key; QLineEdit* edit; QLabel* status; };
    Row makeRow(QGridLayout* grid, int gridRow, const QString& key, const QString& label, const QString& value, const QString& tooltip);
    void refreshRow(Row&);
    void refreshGlobalStatus();
    std::vector<Row> m_rows;
    QSpinBox* m_threads; QCheckBox *m_flash, *m_mmap, *m_unload;
    QLabel *m_libStatus, *m_globalStatus;
};

class SdDialogBase : public MovableDialog {
    Q_OBJECT
public:
    ~SdDialogBase() override;
    void accept() override;
    void reject() override;

    bool generate();                 // lance la génération (faux + message d'état si impossible) ; non bloquant
    void cancelGeneration();         // demande l'arrêt ; le dialogue reste ouvert jusqu'à la fin effective du fil
    bool running() const;
    int resultCount() const { return int(m_results.size()); }
    int currentResult() const;
    cv::Mat selectedImage() const;   // variante choisie (BGRA), vide si aucune
    QString promptText() const;
    qint64 lastSeed() const { return m_lastSeed; }
    QString statusText() const;

signals:
    void generationFinished(bool ok, bool cancelled);

protected:
    SdDialogBase(const QString& title, QWidget* parent);
    QFormLayout* specificForm() const { return m_specific; }
    QVBoxLayout* rightColumn() const { return m_rightCol; }   // colonne de droite (aperçu…), au-dessus de la liste des variantes
    void finishLayout(const QString& okText);          // à appeler en fin de constructeur des classes dérivées
    virtual bool buildRequest(Sd::Request& r, QString* error) = 0;   // width/height/initImage/mask (+ mode) ; le reste est rempli par la base
    virtual void resultsReady() {}
    virtual void currentChanged(int index) { Q_UNUSED(index); }
    virtual void aboutToClose() {}                     // restaurer l'aperçu, etc. (accept ET reject)
    virtual Sd::Request::Mode mode() const = 0;
    virtual QString uiKey() const = 0;                  // espace de noms des réglages mémorisés (« gen », « inpaint »…) : chaque dialogue a les siens
    QString key(const char* name) const { return QString("sd_ui/%1/%2").arg(uiKey(), name); }
    void setStatus(const QString& text, bool error);

    QPlainTextEdit *m_prompt, *m_negative;
    QListWidget* m_list;

private:
    void onResult(const Sd::Result& r);
    void setRunning(bool on);
    void loadUi();
    void saveUi();
    QVBoxLayout *m_root, *m_rightCol; QFormLayout* m_specific; QScrollArea* m_scroll; QWidget* m_body;
    QSpinBox *m_steps, *m_batch, *m_seed; QDoubleSpinBox* m_cfg; QCheckBox *m_randomSeed, *m_tiling;
    QComboBox *m_sampler, *m_scheduler;
    QProgressBar* m_bar; QLabel *m_stage, *m_statusLabel, *m_seedInfo;
    QPushButton *m_generate, *m_cancel, *m_ok, *m_cancelDlg; QToolButton* m_logToggle; QPlainTextEdit* m_log;
    QPointer<Sd::Job> m_job;
    std::vector<cv::Mat> m_results;
    qint64 m_lastSeed = 0; bool m_closing = false, m_active = false;   // m_active : un calcul est en cours (jusqu'à la réception du résultat)
};

class SdGenerateDialog : public SdDialogBase {
    Q_OBJECT
public:
    SdGenerateDialog(Document* docOrNull, QWidget* parent);
    Sd::Placement placement() const;
protected:
    bool buildRequest(Sd::Request& r, QString* error) override;
    void resultsReady() override { updatePreview(); }
    void currentChanged(int) override { updatePreview(); }
    void aboutToClose() override;
    Sd::Request::Mode mode() const override { return Sd::Request::Txt2Img; }
    QString uiKey() const override { return "gen"; }
private:
    void updatePreview();
    Document* m_doc;
    QSpinBox *m_w, *m_h; QComboBox* m_placement; QLabel* m_preview;
};

class SdInpaintDialog : public SdDialogBase {
    Q_OBJECT
public:
    SdInpaintDialog(Document* doc, QWidget* parent);   // le calque actif doit être modifiable et une sélection présente
    const Sd::InpaintPlan& plan() const { return m_plan; }
protected:
    bool buildRequest(Sd::Request& r, QString* error) override;
    void resultsReady() override { showPreview(); }
    void currentChanged(int) override { showPreview(); }
    void aboutToClose() override { restorePreview(); }
    Sd::Request::Mode mode() const override { return Sd::Request::Inpaint; }
    QString uiKey() const override { return "inpaint"; }
private:
    Sd::InpaintPlanParams planParams() const;
    void refreshInfo();
    void showPreview();
    void restorePreview();
    Document* m_doc; Layer::Ptr m_layer; cv::Mat m_orig; Sd::InpaintPlan m_plan;
    QComboBox* m_res; QSpinBox *m_context, *m_expand, *m_feather; SliderSpin* m_strength;
    QCheckBox *m_merged, *m_livePreview; QLabel* m_info; QTimer* m_infoTimer;
};
