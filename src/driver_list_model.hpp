// Language: C++20; file: driver_list_model.hpp; target: Windows 10/11; runtime: Qt 6/MSVC.
#pragma once

#include "update_orchestrator.hpp"

#include <QAbstractListModel>

class DriverListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int selectedCount READ selected_count NOTIFY selectedCountChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        DeviceClassRole,
        InstalledVersionRole,
        AvailableVersionRole,
        AvailableDateRole,
        SourceRole,
        SelectedRole,
        InstanceIdRole,
        DetailsRole,
    };
    Q_ENUM(Role)

    explicit DriverListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;

    int selected_count() const;
    void set_offers(std::vector<open_driver::UpdateOffer> offers, bool select_all);
    void clear();
    const std::vector<open_driver::UpdateOffer>& offers() const;
    std::vector<std::size_t> selected_indices() const;

    Q_INVOKABLE void setSelected(int row, bool selected);
    Q_INVOKABLE void selectAll(bool selected);

signals:
    void selectedCountChanged();
    void countChanged();

private:
    std::vector<open_driver::UpdateOffer> offers_;
    std::vector<bool> selected_;
};
