// Language: C++20; file: app_controller.hpp; target: Windows 10/11; runtime: Qt 6/MSVC.
#pragma once

#include "driver_list_model.hpp"

#include <QObject>
#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <thread>

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stageText READ stage_text NOTIFY progressChanged)
    Q_PROPERTY(QString detailText READ detail_text NOTIFY progressChanged)
    Q_PROPERTY(QString summaryText READ summary_text NOTIFY summaryChanged)
    Q_PROPERTY(QString errorText READ error_text NOTIFY summaryChanged)
    Q_PROPERTY(int progressCurrent READ progress_current NOTIFY progressChanged)
    Q_PROPERTY(int progressTotal READ progress_total NOTIFY progressChanged)
    Q_PROPERTY(bool progressDeterminate READ progress_determinate NOTIFY progressChanged)
    Q_PROPERTY(int devicesAnalyzed READ devices_analyzed NOTIFY summaryChanged)
    Q_PROPERTY(DriverListModel* drivers READ drivers CONSTANT)
    Q_PROPERTY(QVariantMap systemInfo READ system_info NOTIFY systemInfoChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(bool animationsEnabled READ animations_enabled WRITE set_animations_enabled NOTIFY settingsChanged)
    Q_PROPERTY(bool autoSelectUpdates READ auto_select_updates WRITE set_auto_select_updates NOTIFY settingsChanged)
    Q_PROPERTY(bool showAdvancedDetails READ show_advanced_details WRITE set_show_advanced_details NOTIFY settingsChanged)
    Q_PROPERTY(bool reducedMotion READ reduced_motion CONSTANT)
    Q_PROPERTY(bool integrityBusy READ integrity_busy NOTIFY integrityChanged)
    Q_PROPERTY(QString integritySummary READ integrity_summary NOTIFY integrityChanged)
    Q_PROPERTY(QString dataFolder READ data_folder CONSTANT)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    QString state() const;
    QString stage_text() const;
    QString detail_text() const;
    QString summary_text() const;
    QString error_text() const;
    int progress_current() const;
    int progress_total() const;
    bool progress_determinate() const;
    int devices_analyzed() const;
    DriverListModel* drivers();
    QVariantMap system_info() const;
    QVariantList history() const;
    bool animations_enabled() const;
    bool auto_select_updates() const;
    bool show_advanced_details() const;
    bool reduced_motion() const;
    bool integrity_busy() const;
    QString integrity_summary() const;
    QString data_folder() const;

    void set_animations_enabled(bool enabled);
    void set_auto_select_updates(bool enabled);
    void set_show_advanced_details(bool enabled);

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void cancelOperation();
    Q_INVOKABLE void authorizeCandidateDownloads(bool allowed);
    Q_INVOKABLE void requestUpdateSelected();
    Q_INVOKABLE void requestUpdateOne(int row);
    Q_INVOKABLE void confirmInstallation(bool allowed);
    Q_INVOKABLE void restartNow();
    Q_INVOKABLE void refreshHistory();
    Q_INVOKABLE void openDataFolder();
    Q_INVOKABLE void exportDiagnostics();
    Q_INVOKABLE void startIntegrityCheck();

signals:
    void stateChanged();
    void progressChanged();
    void summaryChanged();
    void systemInfoChanged();
    void historyChanged();
    void settingsChanged();
    void integrityChanged();
    void downloadAuthorizationRequested(int packageCount, QString estimatedSize);
    void installationConfirmationRequested(int driverCount);
    void toastRequested(QString message, bool error);

private:
    void set_state(QString state);
    void set_progress(QString stage, QString detail, int current, int total);
    void post_progress(std::uint64_t generation, QString stage, QString detail,
                       std::size_t current, std::size_t total);
    void finish_cancelled(std::uint64_t generation);
    void begin_validation();
    void begin_installation(std::vector<std::size_t> indices);
    void load_system_info();
    void update_summary_from_report(const open_driver::UpdateDiscoveryReport& report);
    static std::filesystem::path application_data_root();

    DriverListModel drivers_;
    QString state_{QStringLiteral("idle")};
    QString stage_text_{QStringLiteral("Pronto para verificar")};
    QString detail_text_{QStringLiteral("Todos os componentes serão analisados")};
    QString summary_text_{QStringLiteral("Verifique todos os componentes do computador")};
    QString error_text_;
    int progress_current_{};
    int progress_total_{};
    int devices_analyzed_{};
    QVariantMap system_info_;
    QVariantList history_;
    QSettings settings_{QStringLiteral("OpenDriver"), QStringLiteral("OpenDriver")};
    bool reduced_motion_{};
    bool integrity_busy_{};
    QString integrity_summary_{QStringLiteral("Ainda não analisado")};
    std::unique_ptr<open_driver::UpdateDiscoveryReport> pending_report_;
    std::vector<std::size_t> pending_install_indices_;
    std::jthread worker_;
    std::jthread utility_worker_;
    std::atomic<std::uint64_t> generation_{0};
};
