#pragma once
// Чтение/запись модели в JSON. Формат совместим с веб-версией (version 1):
// недостающие поля получают значения по умолчанию, битые ссылки вычищаются.

#include <expected>
#include <string>
#include <string_view>

#include "ad/model.hpp"

namespace ad {

/// Разобрать и нормализовать модель. Ошибка — если это не JSON или нет nodes/scenario.
std::expected<Model, std::string> parseModel(std::string_view json);

/// Привести модель к согласованному виду: id, размеры, ссылки на узлы/связи/порты.
void normalizeModel(Model& m);

std::string serializeModel(const Model& m, int indent = 2);

}  // namespace ad
