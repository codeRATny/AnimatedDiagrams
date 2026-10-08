#pragma once

#include "ad/model.hpp"

namespace ad {

/// Пример: A → B (недоступен) → таймаут 5с + ретраи → fallback на C → 200 OK.
Model sampleModel();

/// Пустая диаграмма.
Model emptyModel();

}  // namespace ad
