// Language: C++20; file: driver_list_model.cpp; target: Windows 10/11; runtime: Qt 6/MSVC.
#include "driver_list_model.hpp"

#include <QString>

namespace {

QString text(const std::string& value) {
    return QString::fromUtf8(value);
}

QString device_name(const open_driver::DeviceRecord& device) {
    if (!device.friendly_name.empty()) return text(device.friendly_name);
    if (!device.description.empty()) return text(device.description);
    return text(device.installed_driver.description);
}

QString friendly_date(std::uint32_t date) {
    if (date == 0) return QStringLiteral("—");
    return QStringLiteral("%1/%2/%3")
        .arg(date % 100U, 2, 10, QLatin1Char('0'))
        .arg((date / 100U) % 100U, 2, 10, QLatin1Char('0'))
        .arg(date / 10000U, 4, 10, QLatin1Char('0'));
}

}  // namespace

DriverListModel::DriverListModel(QObject* parent) : QAbstractListModel(parent) {}

int DriverListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(offers_.size());
}

QVariant DriverListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto row = static_cast<std::size_t>(index.row());
    const auto& offer = offers_[row];
    switch (role) {
        case NameRole: return device_name(offer.device);
        case DeviceClassRole: return text(offer.device.class_name);
        case InstalledVersionRole: return text(offer.device.installed_driver.version);
        case AvailableVersionRole: return text(offer.available_version);
        case AvailableDateRole: return friendly_date(offer.available_date_yyyymmdd);
        case SourceRole: return text(offer.source_name);
        case SelectedRole: return selected_[row];
        case InstanceIdRole: return text(offer.device.instance_id);
        case DetailsRole:
            return QStringLiteral("Fornecedor: %1\nINF atual: %2\nHardware: %3")
                .arg(text(offer.device.installed_driver.provider),
                     text(offer.device.installed_driver.inf_path),
                     offer.device.hardware_ids.empty()
                         ? QStringLiteral("não informado") : text(offer.device.hardware_ids.front()));
        default: return {};
    }
}

bool DriverListModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != SelectedRole || !index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return false;
    }
    const auto row = static_cast<std::size_t>(index.row());
    const bool selected = value.toBool();
    if (selected_[row] == selected) return true;
    selected_[row] = selected;
    emit dataChanged(index, index, {SelectedRole});
    emit selectedCountChanged();
    return true;
}

Qt::ItemFlags DriverListModel::flags(const QModelIndex& index) const {
    return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
}

QHash<int, QByteArray> DriverListModel::roleNames() const {
    return {{NameRole, "driverName"}, {DeviceClassRole, "deviceClass"},
            {InstalledVersionRole, "installedVersion"},
            {AvailableVersionRole, "availableVersion"},
            {AvailableDateRole, "availableDate"}, {SourceRole, "sourceName"},
            {SelectedRole, "selected"}, {InstanceIdRole, "instanceId"},
            {DetailsRole, "details"}};
}

int DriverListModel::selected_count() const {
    return static_cast<int>(std::count(selected_.begin(), selected_.end(), true));
}

void DriverListModel::set_offers(std::vector<open_driver::UpdateOffer> offers, bool select_all) {
    beginResetModel();
    offers_ = std::move(offers);
    selected_.assign(offers_.size(), select_all);
    endResetModel();
    emit countChanged();
    emit selectedCountChanged();
}

void DriverListModel::clear() {
    set_offers({}, false);
}

const std::vector<open_driver::UpdateOffer>& DriverListModel::offers() const {
    return offers_;
}

std::vector<std::size_t> DriverListModel::selected_indices() const {
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < selected_.size(); ++index) {
        if (selected_[index]) result.push_back(index);
    }
    return result;
}

void DriverListModel::setSelected(int row, bool selected) {
    setData(index(row), selected, SelectedRole);
}

void DriverListModel::selectAll(bool selected) {
    if (selected_.empty()) return;
    std::fill(selected_.begin(), selected_.end(), selected);
    emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
    emit selectedCountChanged();
}
