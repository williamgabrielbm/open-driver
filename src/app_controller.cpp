// Language: C++20; file: app_controller.cpp; target: Windows 10/11; runtime: Qt 6/MSVC.
#include "app_controller.hpp"

#include "device_inventory.hpp"

#include <windows.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <fstream>
#include <numeric>
#include <set>

namespace {

QString utf8(const std::string& value) {
    return QString::fromUtf8(value);
}

QString device_name(const open_driver::DeviceRecord& device) {
    if (!device.friendly_name.empty()) return utf8(device.friendly_name);
    if (!device.description.empty()) return utf8(device.description);
    return utf8(device.installed_driver.description);
}

QString friendly_size(std::uint64_t bytes) {
    if (bytes == 0) return QStringLiteral("tamanho não informado");
    const double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (megabytes < 1024.0) return QString::number(megabytes, 'f', 1) + QStringLiteral(" MB");
    return QString::number(megabytes / 1024.0, 'f', 2) + QStringLiteral(" GB");
}

QString processor_name() {
    QSettings registry(
        QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
        QSettings::NativeFormat);
    return registry.value(QStringLiteral("ProcessorNameString"),
                          QStringLiteral("Processador detectado")).toString().trimmed();
}

QString last_journal_error(const std::filesystem::path& path) {
    QFile file(QString::fromStdWString(path.wstring()));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString message;
    while (!file.atEnd()) {
        const auto document = QJsonDocument::fromJson(file.readLine());
        if (!document.isObject()) continue;
        const auto object = document.object();
        const auto status = object.value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("failed") || status == QStringLiteral("blocked")) {
            message = object.value(QStringLiteral("message")).toString();
        }
    }
    return message;
}

struct InstallResult {
    bool success{};
    bool reboot_required{};
    QString message;
};

InstallResult execute_prepared_update(const open_driver::PreparedUpdate& update) {
    const auto executable = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("open_driver_transaction.exe"));
    if (!QFile::exists(executable)) {
        return {.message = QStringLiteral("O executor transacional não foi encontrado.")};
    }
    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("--plan"), QString::fromStdWString(update.plan_path.wstring()),
                          QStringLiteral("--journal"), QString::fromStdWString(update.journal_path.wstring()),
                          QStringLiteral("--apply"), QStringLiteral("--confirm"),
                          utf8(update.plan_id)});
    process.start();
    if (!process.waitForStarted(15000)) {
        return {.message = QStringLiteral("O Windows não iniciou a instalação: ") + process.errorString()};
    }
    process.waitForFinished(-1);
    const int code = process.exitCode();
    const bool reboot = code == ERROR_SUCCESS_REBOOT_REQUIRED;
    if (process.exitStatus() != QProcess::NormalExit || (code != 0 && !reboot)) {
        auto detail = last_journal_error(update.journal_path);
        if (detail.isEmpty()) detail = QStringLiteral("consulte o histórico técnico da operação");
        return {.success = false, .reboot_required = reboot,
                .message = QStringLiteral("A instalação foi interrompida: ") + detail};
    }
    return {.success = true, .reboot_required = reboot};
}

}  // namespace

AppController::AppController(QObject* parent) : QObject(parent) {
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    reduced_motion_ = animations == FALSE;
    refreshHistory();
    QTimer::singleShot(0, this, [this] { load_system_info(); });
}

AppController::~AppController() {
    worker_.request_stop();
    utility_worker_.request_stop();
}

QString AppController::state() const { return state_; }
QString AppController::stage_text() const { return stage_text_; }
QString AppController::detail_text() const { return detail_text_; }
QString AppController::summary_text() const { return summary_text_; }
QString AppController::error_text() const { return error_text_; }
int AppController::progress_current() const { return progress_current_; }
int AppController::progress_total() const { return progress_total_; }
bool AppController::progress_determinate() const { return progress_total_ > 0; }
int AppController::devices_analyzed() const { return devices_analyzed_; }
DriverListModel* AppController::drivers() { return &drivers_; }
QVariantMap AppController::system_info() const { return system_info_; }
QVariantList AppController::history() const { return history_; }
bool AppController::animations_enabled() const {
    return settings_.value(QStringLiteral("ui/animations"), true).toBool() && !reduced_motion_;
}
bool AppController::auto_select_updates() const {
    return settings_.value(QStringLiteral("scan/autoSelect"), true).toBool();
}
bool AppController::show_advanced_details() const {
    return settings_.value(QStringLiteral("ui/showAdvancedDetails"), false).toBool();
}
bool AppController::reduced_motion() const { return reduced_motion_; }
bool AppController::integrity_busy() const { return integrity_busy_; }
QString AppController::integrity_summary() const { return integrity_summary_; }
QString AppController::data_folder() const {
    return QString::fromStdWString(application_data_root().wstring());
}

void AppController::set_animations_enabled(bool enabled) {
    settings_.setValue(QStringLiteral("ui/animations"), enabled);
    emit settingsChanged();
}

void AppController::set_auto_select_updates(bool enabled) {
    settings_.setValue(QStringLiteral("scan/autoSelect"), enabled);
    emit settingsChanged();
}

void AppController::set_show_advanced_details(bool enabled) {
    settings_.setValue(QStringLiteral("ui/showAdvancedDetails"), enabled);
    emit settingsChanged();
}

void AppController::set_state(QString state) {
    if (state_ == state) return;
    state_ = std::move(state);
    emit stateChanged();
}

void AppController::set_progress(
    QString stage, QString detail, int current, int total) {
    stage_text_ = std::move(stage);
    detail_text_ = std::move(detail);
    progress_current_ = current;
    progress_total_ = total;
    emit progressChanged();
}

void AppController::post_progress(
    std::uint64_t generation, QString stage, QString detail,
    std::size_t current, std::size_t total) {
    QMetaObject::invokeMethod(this, [this, generation, stage = std::move(stage),
                                     detail = std::move(detail), current, total] {
        if (generation != generation_.load()) return;
        set_progress(stage, detail, static_cast<int>(current), static_cast<int>(total));
    }, Qt::QueuedConnection);
}

void AppController::startScan() {
    static const std::set<QString> busy_states{
        QStringLiteral("scanning"), QStringLiteral("validating"),
        QStringLiteral("downloading"), QStringLiteral("installing"),
        QStringLiteral("cancelling")};
    if (busy_states.contains(state_)) return;
    worker_.request_stop();
    if (worker_.joinable()) worker_.join();
    const auto generation = ++generation_;
    drivers_.clear();
    pending_report_.reset();
    error_text_.clear();
    summary_text_ = QStringLiteral("Verificando todos os componentes do computador");
    devices_analyzed_ = 0;
    emit summaryChanged();
    set_progress(QStringLiteral("Preparando a verificação"),
                 QStringLiteral("Lendo o inventário de hardware"), 0, 0);
    set_state(QStringLiteral("scanning"));

    worker_ = std::jthread([this, generation](std::stop_token stop) {
        auto inventory = open_driver::collect_device_inventory({.present_only = true});
        if (stop.stop_requested()) {
            finish_cancelled(generation);
            return;
        }
        if (inventory.fatal_error) {
            const auto error = utf8(inventory.fatal_error->message);
            QMetaObject::invokeMethod(this, [this, generation, error] {
                if (generation != generation_.load()) return;
                error_text_ = error;
                summary_text_ = QStringLiteral("A verificação não pôde ser concluída");
                set_state(QStringLiteral("error"));
                emit summaryChanged();
            }, Qt::QueuedConnection);
            return;
        }
        auto report = std::make_shared<open_driver::UpdateDiscoveryReport>(
            open_driver::discover_updates(inventory.devices,
                [this, generation](std::size_t current, std::size_t total, const std::string& name) {
                    post_progress(generation, QStringLiteral("Verificando dispositivos"),
                                  utf8(name), current, total);
                }, stop));
        if (stop.stop_requested() || report->cancelled) {
            finish_cancelled(generation);
            return;
        }
        QMetaObject::invokeMethod(this, [this, generation, report] {
            if (generation != generation_.load()) return;
            devices_analyzed_ = static_cast<int>(report->devices_checked);
            if (!report->complete) {
                error_text_ = report->errors.empty()
                    ? QStringLiteral("Uma fonte obrigatória não concluiu a consulta.")
                    : utf8(report->errors.front());
                summary_text_ = QStringLiteral("A verificação ficou incompleta");
                set_state(QStringLiteral("error"));
                emit summaryChanged();
                return;
            }
            if (report->offers.empty()) {
                drivers_.clear();
                pending_report_.reset();
                update_summary_from_report(*report);
                set_state(QStringLiteral("results"));
                return;
            }
            std::set<std::string> packages;
            std::uint64_t bytes = 0;
            for (const auto& offer : report->offers) {
                const auto key = offer.source_package_id.empty()
                    ? offer.search.catalog_update_id : offer.source_package_id;
                if (packages.insert(key).second) bytes += offer.estimated_download_bytes;
            }
            pending_report_ = std::make_unique<open_driver::UpdateDiscoveryReport>(std::move(*report));
            set_progress(QStringLiteral("Candidatos encontrados"),
                         QStringLiteral("Aguardando autorização para validar os pacotes"), 0, 0);
            set_state(QStringLiteral("awaitingDownload"));
            emit downloadAuthorizationRequested(static_cast<int>(packages.size()), friendly_size(bytes));
        }, Qt::QueuedConnection);
    });
}

void AppController::cancelOperation() {
    if (state_ != QStringLiteral("scanning") && state_ != QStringLiteral("validating") &&
        state_ != QStringLiteral("downloading") && state_ != QStringLiteral("installing")) return;
    worker_.request_stop();
    set_progress(QStringLiteral("Interrompendo..."),
                 state_ == QStringLiteral("installing")
                     ? QStringLiteral("A etapa crítica atual será concluída antes de parar")
                     : QStringLiteral("Cancelando consultas e tarefas pendentes"),
                 progress_current_, progress_total_);
    set_state(QStringLiteral("cancelling"));
}

void AppController::finish_cancelled(std::uint64_t generation) {
    QMetaObject::invokeMethod(this, [this, generation] {
        if (generation != generation_.load()) return;
        pending_report_.reset();
        drivers_.clear();
        summary_text_ = QStringLiteral("Verificação interrompida com segurança");
        set_progress(QStringLiteral("Cancelado"),
                     QStringLiteral("Nenhum resultado atrasado será aplicado"), 0, 0);
        set_state(QStringLiteral("cancelled"));
        emit summaryChanged();
        QTimer::singleShot(700, this, [this, generation] {
            if (generation == generation_.load() && state_ == QStringLiteral("cancelled")) {
                summary_text_ = QStringLiteral("Verifique todos os componentes do computador");
                set_progress(QStringLiteral("Pronto para verificar"),
                             QStringLiteral("Todos os componentes serão analisados"), 0, 0);
                set_state(QStringLiteral("idle"));
                emit summaryChanged();
            }
        });
    }, Qt::QueuedConnection);
}

void AppController::authorizeCandidateDownloads(bool allowed) {
    if (state_ != QStringLiteral("awaitingDownload") || !pending_report_) return;
    if (!allowed) {
        pending_report_.reset();
        drivers_.clear();
        summary_text_ = QStringLiteral("Download não autorizado");
        error_text_ = QStringLiteral("Nenhum pacote foi baixado ou instalado.");
        set_state(QStringLiteral("results"));
        emit summaryChanged();
        return;
    }
    begin_validation();
}

void AppController::begin_validation() {
    if (!pending_report_) return;
    worker_.request_stop();
    if (worker_.joinable()) worker_.join();
    const auto generation = ++generation_;
    auto report = std::shared_ptr<open_driver::UpdateDiscoveryReport>(pending_report_.release());
    set_progress(QStringLiteral("Baixando e validando"),
                 QStringLiteral("Conferindo assinatura, integridade e compatibilidade"), 0,
                 static_cast<int>(report->offers.size()));
    set_state(QStringLiteral("validating"));
    worker_ = std::jthread([this, generation, report](std::stop_token stop) {
        open_driver::validate_and_prepare_offers(
            *report, application_data_root(),
            [this, generation](std::size_t current, std::size_t total, const std::string& name) {
                post_progress(generation, QStringLiteral("Baixando e validando"),
                              utf8(name), current, total);
            }, stop);
        if (stop.stop_requested() || report->cancelled) {
            finish_cancelled(generation);
            return;
        }
        QMetaObject::invokeMethod(this, [this, generation, report] {
            if (generation != generation_.load()) return;
            if (!report->complete) {
                error_text_ = report->errors.empty()
                    ? QStringLiteral("A validação dos pacotes não terminou.")
                    : utf8(report->errors.front());
                summary_text_ = QStringLiteral("A validação ficou incompleta");
                drivers_.clear();
                set_state(QStringLiteral("error"));
                emit summaryChanged();
                return;
            }
            drivers_.set_offers(std::move(report->offers), auto_select_updates());
            update_summary_from_report(*report);
            set_state(QStringLiteral("results"));
        }, Qt::QueuedConnection);
    });
}

void AppController::update_summary_from_report(const open_driver::UpdateDiscoveryReport& report) {
    devices_analyzed_ = static_cast<int>(report.devices_checked);
    const auto count = drivers_.rowCount();
    summary_text_ = count == 0
        ? QStringLiteral("Seus drivers compatíveis estão atualizados")
        : QStringLiteral("%1 %2 %3 atualização")
            .arg(count)
            .arg(count == 1 ? QStringLiteral("driver precisa de")
                            : QStringLiteral("drivers precisam de"))
            .arg(count == 1 ? QString() : QString());
    error_text_.clear();
    if (!report.rejections.empty()) {
        error_text_ = QStringLiteral("%1 candidato(s) foram descartados pela validação final.")
                          .arg(report.candidates_rejected);
    }
    emit summaryChanged();
}

void AppController::requestUpdateSelected() {
    pending_install_indices_ = drivers_.selected_indices();
    if (pending_install_indices_.empty()) {
        emit toastRequested(QStringLiteral("Selecione pelo menos um driver."), true);
        return;
    }
    emit installationConfirmationRequested(static_cast<int>(pending_install_indices_.size()));
}

void AppController::requestUpdateOne(int row) {
    if (row < 0 || row >= drivers_.rowCount()) return;
    pending_install_indices_ = {static_cast<std::size_t>(row)};
    emit installationConfirmationRequested(1);
}

void AppController::confirmInstallation(bool allowed) {
    if (!allowed || pending_install_indices_.empty()) {
        pending_install_indices_.clear();
        return;
    }
    begin_installation(std::exchange(pending_install_indices_, {}));
}

void AppController::begin_installation(std::vector<std::size_t> indices) {
    worker_.request_stop();
    if (worker_.joinable()) worker_.join();
    const auto generation = ++generation_;
    std::vector<open_driver::UpdateOffer> offers;
    for (const auto index : indices) {
        if (index < drivers_.offers().size()) offers.push_back(drivers_.offers()[index]);
    }
    set_progress(QStringLiteral("Preparando instalação"),
                 QStringLiteral("Revalidando o estado atual dos dispositivos"), 0,
                 static_cast<int>(offers.size()));
    set_state(QStringLiteral("installing"));
    worker_ = std::jthread([this, generation, offers = std::move(offers)](std::stop_token stop) {
        bool reboot = false;
        int installed = 0;
        QString failure;
        for (std::size_t index = 0; index < offers.size(); ++index) {
            if (stop.stop_requested()) break;
            const auto inventory = open_driver::collect_device_inventory({.present_only = true});
            if (inventory.fatal_error) {
                failure = QStringLiteral("Não foi possível revalidar o dispositivo: ") +
                          utf8(inventory.fatal_error->message);
                break;
            }
            const auto current = std::find_if(inventory.devices.begin(), inventory.devices.end(),
                [&](const auto& device) {
                    return _stricmp(device.instance_id.c_str(), offers[index].device.instance_id.c_str()) == 0;
                });
            if (current == inventory.devices.end() ||
                !open_driver::device_snapshot_matches_offer(offers[index], *current)) {
                continue;
            }
            post_progress(generation, QStringLiteral("Instalando driver"),
                          device_name(offers[index].device), index + 1, offers.size());
            if (!offers[index].prepared) {
                failure = QStringLiteral("O pacote deixou de estar preparado para instalação.");
                break;
            }
            const auto result = execute_prepared_update(*offers[index].prepared);
            reboot = reboot || result.reboot_required;
            if (!result.success) {
                failure = result.message;
                break;
            }
            ++installed;
        }
        QMetaObject::invokeMethod(this, [this, generation, installed, reboot,
                                         failure = std::move(failure), stopped = stop.stop_requested()] {
            if (generation != generation_.load()) return;
            refreshHistory();
            if (!failure.isEmpty()) {
                error_text_ = failure;
                summary_text_ = QStringLiteral("A instalação foi interrompida");
                set_state(QStringLiteral("error"));
            } else if (stopped) {
                summary_text_ = QStringLiteral("Instalação interrompida em um ponto seguro");
                set_state(QStringLiteral("cancelled"));
            } else {
                summary_text_ = QStringLiteral("%1 pacote(s) instalado(s) com sucesso").arg(installed);
                set_state(reboot ? QStringLiteral("restartRequired")
                                 : QStringLiteral("completed"));
            }
            emit summaryChanged();
        }, Qt::QueuedConnection);
    });
}

void AppController::restartNow() {
    QProcess::startDetached(QStringLiteral("shutdown.exe"),
                            {QStringLiteral("/r"), QStringLiteral("/t"), QStringLiteral("0")});
}

void AppController::refreshHistory() {
    QVariantList entries;
    const QDir journal_dir(QString::fromStdWString((application_data_root() / L"journals").wstring()));
    const auto files = journal_dir.entryInfoList({QStringLiteral("*.jsonl")}, QDir::Files, QDir::Time);
    for (const auto& info : files.mid(0, 50)) {
        QFile file(info.absoluteFilePath());
        QString action = QStringLiteral("Operação de driver");
        QString result = QStringLiteral("Registrada");
        QString detail;
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!file.atEnd()) {
                const auto document = QJsonDocument::fromJson(file.readLine());
                if (!document.isObject()) continue;
                const auto object = document.object();
                action = object.value(QStringLiteral("step")).toString(action);
                result = object.value(QStringLiteral("status")).toString(result);
                detail = object.value(QStringLiteral("message")).toString(detail);
            }
        }
        entries.push_back(QVariantMap{{QStringLiteral("date"), info.lastModified().toString(QStringLiteral("dd/MM/yyyy HH:mm"))},
                                      {QStringLiteral("device"), info.baseName().section('-', 0, 2)},
                                      {QStringLiteral("action"), action},
                                      {QStringLiteral("result"), result},
                                      {QStringLiteral("detail"), detail}});
    }
    history_ = std::move(entries);
    emit historyChanged();
}

void AppController::openDataFolder() {
    QDir().mkpath(data_folder());
    QDesktopServices::openUrl(QUrl::fromLocalFile(data_folder()));
}

void AppController::exportDiagnostics() {
    if (utility_worker_.joinable()) utility_worker_.join();
    utility_worker_ = std::jthread([this](std::stop_token stop) {
        const auto inventory = open_driver::collect_device_inventory({.present_only = true});
        if (stop.stop_requested()) return;
        const auto directory = application_data_root() / L"diagnostics";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        const auto file = directory / std::filesystem::path(
            "inventory-" + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")).toStdString() + ".json");
        std::string write_error;
        const bool written = !inventory.fatal_error &&
            open_driver::write_inventory_json(inventory, file.string(), write_error);
        QMetaObject::invokeMethod(this, [this, written, file, write_error] {
            emit toastRequested(written
                ? QStringLiteral("Diagnóstico salvo em %1").arg(QString::fromStdWString(file.wstring()))
                : QStringLiteral("Falha ao exportar: %1").arg(utf8(write_error)), !written);
        }, Qt::QueuedConnection);
    });
}

void AppController::startIntegrityCheck() {
    if (integrity_busy_) return;
    if (utility_worker_.joinable()) utility_worker_.join();
    integrity_busy_ = true;
    integrity_summary_ = QStringLiteral("Analisando o estado real dos dispositivos...");
    emit integrityChanged();
    utility_worker_ = std::jthread([this](std::stop_token stop) {
        const auto inventory = open_driver::collect_device_inventory({.present_only = true});
        if (stop.stop_requested()) return;
        const auto problems = std::count_if(inventory.devices.begin(), inventory.devices.end(),
            [](const auto& device) { return device.problem_code != 0; });
        const auto total = inventory.devices.size();
        QMetaObject::invokeMethod(this, [this, total, problems, fatal = inventory.fatal_error.has_value()] {
            integrity_busy_ = false;
            integrity_summary_ = fatal
                ? QStringLiteral("O Windows não permitiu concluir a leitura.")
                : problems == 0
                    ? QStringLiteral("%1 dispositivos presentes, sem erro informado pelo Windows.").arg(total)
                    : QStringLiteral("%1 de %2 dispositivos exigem atenção no Windows.").arg(problems).arg(total);
            emit integrityChanged();
        }, Qt::QueuedConnection);
    });
}

void AppController::load_system_info() {
    const auto target = open_driver::current_windows_target();
    MEMORYSTATUSEX memory{sizeof(memory)};
    GlobalMemoryStatusEx(&memory);
    system_info_ = {{QStringLiteral("windows"), target.windows_11
                        ? QStringLiteral("Microsoft Windows 11") : QStringLiteral("Microsoft Windows 10")},
                    {QStringLiteral("build"), QString::number(target.build)},
                    {QStringLiteral("architecture"), utf8(target.architecture)},
                    {QStringLiteral("processor"), processor_name()},
                    {QStringLiteral("memory"), QStringLiteral("%1 GB").arg(
                        (memory.ullTotalPhys + (1ULL << 29)) >> 30)},
                    {QStringLiteral("graphics"), QStringLiteral("Detectando placa de vídeo...")}};
    emit systemInfoChanged();
    if (utility_worker_.joinable()) utility_worker_.join();
    utility_worker_ = std::jthread([this](std::stop_token stop) {
        const auto inventory = open_driver::collect_device_inventory({.present_only = true});
        if (stop.stop_requested()) return;
        QString graphics = QStringLiteral("Adaptador de vídeo não informado");
        const auto display = std::find_if(inventory.devices.begin(), inventory.devices.end(),
            [](const auto& device) { return _stricmp(device.class_name.c_str(), "Display") == 0; });
        if (display != inventory.devices.end()) graphics = device_name(*display);
        QMetaObject::invokeMethod(this, [this, graphics] {
            system_info_[QStringLiteral("graphics")] = graphics;
            emit systemInfoChanged();
        }, Qt::QueuedConnection);
    });
}

std::filesystem::path AppController::application_data_root() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (size != 0 && size < MAX_PATH) return std::filesystem::path(buffer) / L"OpenDriver";
    return std::filesystem::temp_directory_path() / L"OpenDriver";
}
