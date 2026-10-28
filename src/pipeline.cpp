// Language: C++20; file: pipeline.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "pipeline.hpp"

#include <algorithm>

namespace open_driver {

PipelineReport start_pipeline(
    const std::vector<DeviceRecord>& inventory,
    const std::vector<WindowsUpdateDriverRecord>& updates) {
    PipelineReport report;
    report.inventory_devices = inventory.size();
    report.present_devices = static_cast<std::size_t>(std::count_if(
        inventory.begin(), inventory.end(), [](const auto& device) { return device.present; }));
    report.items.reserve(updates.size());

    for (const auto& update : updates) {
        PipelineItem item{.update = update};
        if (update.matching_instance_ids.empty()) {
            item.status = PipelineStatus::no_device_match;
            item.plain_explanation =
                "O Windows ofereceu este pacote, mas ele nao corresponde a nenhum dispositivo conhecido.";
        } else if (update.matching_present_instance_ids.empty()) {
            item.status = PipelineStatus::skipped_absent_device;
            item.plain_explanation =
                "O pacote corresponde a um dispositivo antigo ou desconectado e foi deixado de lado.";
        } else {
            item.status = PipelineStatus::catalog_lookup_pending;
            item.plain_explanation =
                "O dispositivo esta presente; falta confirmar o pacote exato no catalogo da Microsoft.";
        }
        report.items.push_back(std::move(item));
    }
    return report;
}

void apply_catalog_resolution(
    PipelineItem& item,
    const CatalogResolutionResult& resolution) {
    item.catalog_candidates = resolution.candidates.size();
    item.eligible_catalog_candidates = static_cast<std::size_t>(std::count_if(
        resolution.candidates.begin(), resolution.candidates.end(),
        [](const auto& candidate) { return candidate.eligible; }));
    item.selected_catalog_update_id = resolution.selected_catalog_update_id;
    item.error.clear();
    if (resolution.selected_catalog_update_id) {
        item.status = PipelineStatus::catalog_selected;
        item.plain_explanation =
            "Um unico pacote compativel foi escolhido; o proximo passo e baixar e conferir seus arquivos.";
    } else if (resolution.ambiguous) {
        item.status = PipelineStatus::catalog_ambiguous;
        item.plain_explanation =
            "Mais de um pacote terminou empatado; nenhuma escolha automatica foi feita.";
    } else {
        item.status = PipelineStatus::catalog_lookup_failed;
        item.plain_explanation =
            "Nenhum pacote do catalogo passou pelas regras de compatibilidade.";
    }
}

void apply_catalog_error(PipelineItem& item, std::string error) {
    item.status = PipelineStatus::catalog_lookup_failed;
    item.error = std::move(error);
    item.plain_explanation =
        "A consulta ao catalogo falhou; o dispositivo nao foi encaminhado para instalacao.";
}

std::string pipeline_status_name(PipelineStatus status) {
    switch (status) {
        case PipelineStatus::no_device_match: return "no_device_match";
        case PipelineStatus::skipped_absent_device: return "skipped_absent_device";
        case PipelineStatus::catalog_lookup_pending: return "catalog_lookup_pending";
        case PipelineStatus::catalog_lookup_failed: return "catalog_lookup_failed";
        case PipelineStatus::catalog_ambiguous: return "catalog_ambiguous";
        case PipelineStatus::catalog_selected: return "catalog_selected";
    }
    return "catalog_lookup_failed";
}

}  // namespace open_driver
