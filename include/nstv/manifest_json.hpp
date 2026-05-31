#pragma once

#include "nstv/models.hpp"
#include <iosfwd>
#include <string>

namespace nstv {

Manifest manifestFromJsonTextFast(
  const std::string &text,
  Provider fallbackProvider = Provider::Local,
  const std::string &fallbackSource = ""
);

void writeManifestJson(std::ostream &out, const Manifest &manifest);
std::string manifestToJsonTextFast(const Manifest &manifest);

} // namespace nstv
