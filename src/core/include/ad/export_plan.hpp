#pragma once
// Расчёт кадров и размеров для экспорта (без зависимости от рендерера).

#include <vector>

#include "ad/geometry.hpp"

namespace ad {

/// Моменты времени кадров: 0, 1/fps, ... и ровно `duration` последним кадром.
std::vector<double> exportFrameTimes(double durationMs, double fps);

struct ExportGeometry {
    Rect world;  // область мира, попадающая в кадр
    int pxW = 0;
    int pxH = 0;
};

/// Размер кадра: world × scale, большая сторона ≤ maxDim (с сохранением пропорций),
/// чётные размеры (требование видеокодеков).
ExportGeometry exportGeometry(Rect world, double scale, int maxDim = 12000);

/// Отступ вокруг содержимого — место под кольцо таймера и подписи пакетов.
inline constexpr double kExportPadding = 74;

}  // namespace ad
