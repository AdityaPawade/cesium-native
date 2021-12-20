// This file was generated by generate-classes.
// DO NOT EDIT THIS FILE!
#pragma once

#include "ExtensionExtFeatureMetadataEnumValueJsonHandler.h"

#include <CesiumGltf/ExtensionExtFeatureMetadataEnum.h>
#include <CesiumJsonReader/ArrayJsonHandler.h>
#include <CesiumJsonReader/ExtensibleObjectJsonHandler.h>
#include <CesiumJsonReader/StringJsonHandler.h>

namespace CesiumJsonReader {
class ExtensionReaderContext;
}

namespace CesiumGltfReader {
class ExtensionExtFeatureMetadataEnumJsonHandler
    : public CesiumJsonReader::ExtensibleObjectJsonHandler {
public:
  using ValueType = CesiumGltf::ExtensionExtFeatureMetadataEnum;

  ExtensionExtFeatureMetadataEnumJsonHandler(
      const CesiumJsonReader::ExtensionReaderContext& context) noexcept;
  void reset(
      IJsonHandler* pParentHandler,
      CesiumGltf::ExtensionExtFeatureMetadataEnum* pObject);

  virtual IJsonHandler* readObjectKey(const std::string_view& str) override;

protected:
  IJsonHandler* readObjectKeyExtensionExtFeatureMetadataEnum(
      const std::string& objectType,
      const std::string_view& str,
      CesiumGltf::ExtensionExtFeatureMetadataEnum& o);

private:
  CesiumGltf::ExtensionExtFeatureMetadataEnum* _pObject = nullptr;
  CesiumJsonReader::StringJsonHandler _name;
  CesiumJsonReader::StringJsonHandler _description;
  CesiumJsonReader::StringJsonHandler _valueType;
  CesiumJsonReader::ArrayJsonHandler<
      CesiumGltf::ExtensionExtFeatureMetadataEnumValue,
      ExtensionExtFeatureMetadataEnumValueJsonHandler>
      _values;
};
} // namespace CesiumGltfReader