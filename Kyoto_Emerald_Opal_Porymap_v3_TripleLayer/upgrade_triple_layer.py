#!/usr/bin/env python3
from __future__ import annotations
from pathlib import Path
import argparse
import re
import shutil
import sys

FILES = [
    "include/core/metatile.h",
    "include/project.h",
    "include/opaltiles.h",
    "include/opalpalettes.h",
    "src/project.cpp",
    "src/core/tileset.cpp",
    "src/ui/tileseteditor.cpp",
    "src/ui/imageproviders.cpp",
]


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text()
    count = text.count(old)
    if count == 0:
        if new in text:
            print(f"already patched: {label}")
            return
        raise SystemExit(f"{path}: could not find patch point for {label}")
    if count != 1:
        raise SystemExit(f"{path}: patch point for {label} occurs {count} times")
    path.write_text(text.replace(old, new, 1))
    print(f"patched: {label}")


def replace_regex(path: Path, pattern: str, replacement: str, label: str):
    text = path.read_text()
    new_text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count == 0:
        if replacement.strip() in text:
            print(f"already patched: {label}")
            return
        raise SystemExit(f"{path}: could not find regex patch point for {label}")
    path.write_text(new_text)
    print(f"patched: {label}")


def main():
    ap = argparse.ArgumentParser(description="Add Kyoto hybrid triple-layer sidecar authoring to the patched Opal Porymap 6.3.1 source.")
    ap.add_argument("--source", type=Path, required=True)
    args = ap.parse_args()
    root = args.source.resolve()

    for rel in FILES:
        if not (root / rel).exists():
            raise SystemExit(f"Missing expected Porymap source file: {root / rel}")

    backup = root / ".kyoto_triple_backup"
    if not backup.exists():
        for rel in FILES:
            target = backup / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(root / rel, target)
        print(f"Backup written to {backup}")

    # 1) Fourth layer type: 3 = true Bottom/Middle/Top.
    replace_once(
        root / "include/core/metatile.h",
        """        Split,\n        Count""",
        """        Split,\n        Triple,\n        Count""",
        "Metatile::LayerType::Triple",
    )

    # 2) Project-level hybrid flag.
    replace_once(
        root / "include/project.h",
        """    static bool usesOpalExtendedTiles() { return opal_extended_tiles; }\n    static int getNumTilesTotal()""",
        """    static bool usesOpalExtendedTiles() { return opal_extended_tiles; }\n    static bool usesKyotoHybridTripleLayers() { return kyoto_hybrid_triple_layers; }\n    static int getNumTilesTotal()""",
        "Project hybrid triple getter",
    )
    replace_once(
        root / "include/project.h",
        """    static bool opal_extended_tiles;\n    static int num_metatiles_primary;""",
        """    static bool opal_extended_tiles;\n    static bool kyoto_hybrid_triple_layers;\n    static int num_metatiles_primary;""",
        "Project hybrid triple storage",
    )

    replace_once(
        root / "src/project.cpp",
        """bool Project::opal_extended_tiles = false;\nint Project::num_metatiles_primary = 512;""",
        """bool Project::opal_extended_tiles = false;\nbool Project::kyoto_hybrid_triple_layers = false;\nint Project::num_metatiles_primary = 512;""",
        "Project hybrid triple static",
    )

    old = """    Project::opal_extended_tiles = QFile::exists(root + \"/include/tile_cache.h\");\n    if (Project::opal_extended_tiles) {\n        Project::num_tiles_primary = OpalTiles::TilesPrimary;\n        Project::num_tiles_total = OpalTiles::TilesTotal;\n    }"""
    new = """    Project::opal_extended_tiles = QFile::exists(root + \"/include/tile_cache.h\");\n    if (Project::opal_extended_tiles) {\n        Project::num_tiles_primary = OpalTiles::TilesPrimary;\n        Project::num_tiles_total = OpalTiles::TilesTotal;\n    }\n\n    // Kyoto keeps the stock 8-word metatiles.bin format, but stores a real third\n    // 2x2 layer in metatile_third_layer.bin.  Keep Porymap's 3-layer editor UI\n    // while preserving the legacy layer-type modes for existing metatiles.\n    Project::kyoto_hybrid_triple_layers = false;\n    QFile kyotoFieldmap(root + \"/include/global.fieldmap.h\");\n    if (kyotoFieldmap.open(QIODevice::ReadOnly)) {\n        const QByteArray text = kyotoFieldmap.readAll();\n        Project::kyoto_hybrid_triple_layers = Project::opal_extended_tiles\n                                           && text.contains(\"METATILE_LAYER_TYPE_TRIPLE\");\n    }\n    if (Project::kyoto_hybrid_triple_layers)\n        projectConfig.tripleLayerMetatilesEnabled = true;"""
    replace_once(root / "src/project.cpp", old, new, "detect Kyoto hybrid triple format")

    # 3) Third-layer binary sidecars.
    opaltiles = root / "include/opaltiles.h"
    insertion = r'''
inline QString thirdLayerPath(const QString &metatilesPath) {
    return QFileInfo(metatilesPath).dir().filePath("metatile_third_layer.bin");
}
inline QString thirdLayerExtPath(const QString &metatilesPath) {
    return QFileInfo(metatilesPath).dir().filePath("metatile_third_layer_ext.bin");
}
inline QByteArray readBinary(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    return file.readAll();
}
inline bool writeBinary(const QString &path, const QByteArray &data, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QString("Could not save %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}
inline QByteArray readThirdLayer(const QString &metatilesPath) {
    return readBinary(thirdLayerPath(metatilesPath));
}
inline QByteArray readThirdLayerExt(const QString &metatilesPath) {
    return readBinary(thirdLayerExtPath(metatilesPath));
}
inline bool writeThirdLayer(const QString &metatilesPath, const QByteArray &data, QString *error) {
    return writeBinary(thirdLayerPath(metatilesPath), data, error);
}
inline bool writeThirdLayerExt(const QString &metatilesPath, const QByteArray &data, QString *error) {
    return writeBinary(thirdLayerExtPath(metatilesPath), data, error);
}
'''.strip("\n")
    text = opaltiles.read_text()
    marker = "\n} // namespace OpalTiles\n"
    if "thirdLayerPath" not in text:
        if marker not in text:
            raise SystemExit("include/opaltiles.h: namespace end not found")
        opaltiles.write_text(text.replace(marker, "\n" + insertion + marker, 1))
        print("patched: Opal third-layer sidecar helpers")
    else:
        print("already patched: Opal third-layer sidecar helpers")

    # 4) Palette refs: mixed 8- and 12-entry rows load safely; saved rows are 12 entries.
    p = root / "include/opalpalettes.h"
    old = '''    QVector<QVector<int>> result;\n    for (const auto &value : root.value("palettes").toArray()) {\n        if (!value.isArray() || value.toArray().size() != columns) return false;\n        QVector<int> row;\n        for (const auto &id : value.toArray()) {\n            if (!id.isDouble() || id.toDouble() != id.toInt(-1) || id.toInt(-1) < 0 || id.toInt() >= 18) return false;\n            row.append(id.toInt());\n        }\n        result.append(row);\n    }'''
    new = '''    QVector<QVector<int>> result;\n    for (const auto &value : root.value("palettes").toArray()) {\n        if (!value.isArray()) return false;\n        const auto values = value.toArray();\n        // Kyoto's older M3 authoring files have 8 refs. Hybrid triple-layer files\n        // use 12. Pad legacy rows with palette 0 for the third layer.\n        if (values.size() != columns && !(columns == 12 && values.size() == 8)) return false;\n        QVector<int> row;\n        for (const auto &id : values) {\n            if (!id.isDouble() || id.toDouble() != id.toInt(-1) || id.toInt(-1) < 0 || id.toInt() >= 18) return false;\n            row.append(id.toInt());\n        }\n        while (row.size() < columns) row.append(0);\n        result.append(row);\n    }'''
    replace_once(p, old, new, "mixed 8/12 palette references")

    # 5) Load/save: 8 words stay in metatiles.bin, 4 words live in third-layer sidecar.
    tileset = root / "src/core/tileset.cpp"
    new_load = r'''bool Tileset::loadMetatiles() {
    clearMetatiles();

    QFile file(this->metatiles_path);
    if (!file.open(QIODevice::ReadOnly)) {
        logError(QString("Could not open '%1' for reading: %2").arg(this->metatiles_path).arg(file.errorString()));
        return false;
    }

    const QByteArray data = file.readAll();
    const bool hybridTriple = Project::usesKyotoHybridTripleLayers();
    const int editorTilesPerMetatile = projectConfig.getNumTilesInMetatile();
    const int storedTilesPerMetatile = hybridTriple ? 8 : editorTilesPerMetatile;
    const int bytesPerMetatile = Tile::sizeInBytes() * storedTilesPerMetatile;
    int numMetatiles = data.length() / bytesPerMetatile;
    if (data.length() % bytesPerMetatile) {
        logError(QString("%1 has an invalid metatile byte count for %2 stored subtiles per metatile")
                 .arg(this->metatiles_path).arg(storedTilesPerMetatile));
        return false;
    }
    if (numMetatiles > maxMetatiles()) {
        logWarn(QString("%1 metatile count %2 exceeds limit of %3. Additional metatiles will be ignored.")
                        .arg(this->name).arg(numMetatiles).arg(maxMetatiles()));
        numMetatiles = maxMetatiles();
    }

    const QByteArray opalExt = Project::usesOpalExtendedTiles() ? OpalTiles::readExt(this->metatiles_path) : QByteArray();
    if (!opalExt.isEmpty() && opalExt.size() != numMetatiles * storedTilesPerMetatile) {
        logError(QString("%1 has %2 entries but expected %3")
                 .arg(OpalTiles::extPath(this->metatiles_path)).arg(opalExt.size())
                 .arg(numMetatiles * storedTilesPerMetatile));
        return false;
    }

    QByteArray thirdLayer;
    QByteArray thirdExt;
    if (hybridTriple) {
        thirdLayer = OpalTiles::readThirdLayer(this->metatiles_path);
        thirdExt = OpalTiles::readThirdLayerExt(this->metatiles_path);
        const int thirdBytes = numMetatiles * Metatile::tilesPerLayer() * Tile::sizeInBytes();
        const int thirdExtBytes = numMetatiles * Metatile::tilesPerLayer();
        if (!thirdLayer.isEmpty() && thirdLayer.size() != thirdBytes) {
            logError(QString("%1 has %2 bytes but expected %3")
                     .arg(OpalTiles::thirdLayerPath(this->metatiles_path)).arg(thirdLayer.size()).arg(thirdBytes));
            return false;
        }
        if (!thirdExt.isEmpty() && thirdExt.size() != thirdExtBytes) {
            logError(QString("%1 has %2 entries but expected %3")
                     .arg(OpalTiles::thirdLayerExtPath(this->metatiles_path)).arg(thirdExt.size()).arg(thirdExtBytes));
            return false;
        }
    }

    QVector<QVector<int>> opalRefs;
    QString error;
    if (!opalPaletteRefsPath.isEmpty()
        && !OpalPalettes::readReferences(opalPaletteRefsPath, data, numMetatiles, editorTilesPerMetatile, opalRefs, error)) {
        logError(error);
        return false;
    }

    for (int i = 0; i < numMetatiles; i++) {
        auto metatile = new Metatile;
        int index = i * bytesPerMetatile;
        for (int j = 0; j < storedTilesPerMetatile; j++) {
            uint16_t tileRaw = static_cast<unsigned char>(data[index++]);
            tileRaw |= static_cast<unsigned char>(data[index++]) << 8;
            Tile tile(tileRaw);
            if (!opalRefs.isEmpty()) tile.palette = opalRefs[i][j];
            if (Project::usesOpalExtendedTiles()) {
                int high = opalExt.isEmpty() ? 0
                    : (static_cast<unsigned char>(opalExt[i * storedTilesPerMetatile + j]) & OpalTiles::ExtHighMask) >> OpalTiles::ExtHighShift;
                tile.tileId = OpalTiles::gameToEditor((tileRaw & 0x3FF) | (high << 10));
            }
            metatile->tiles.append(tile);
        }

        if (hybridTriple) {
            for (int j = 0; j < Metatile::tilesPerLayer(); j++) {
                uint16_t tileRaw = 0;
                if (!thirdLayer.isEmpty()) {
                    int thirdIndex = (i * Metatile::tilesPerLayer() + j) * Tile::sizeInBytes();
                    tileRaw = static_cast<unsigned char>(thirdLayer[thirdIndex]);
                    tileRaw |= static_cast<unsigned char>(thirdLayer[thirdIndex + 1]) << 8;
                }
                Tile tile(tileRaw);
                if (!opalRefs.isEmpty()) tile.palette = opalRefs[i][storedTilesPerMetatile + j];
                if (Project::usesOpalExtendedTiles()) {
                    int high = thirdExt.isEmpty() ? 0
                        : (static_cast<unsigned char>(thirdExt[i * Metatile::tilesPerLayer() + j]) & OpalTiles::ExtHighMask) >> OpalTiles::ExtHighShift;
                    tile.tileId = OpalTiles::gameToEditor((tileRaw & 0x3FF) | (high << 10));
                }
                metatile->tiles.append(tile);
            }
        }
        m_metatiles.append(metatile);
    }
    return true;
}'''

    new_save = r'''bool Tileset::saveMetatiles() {
    QByteArray data;
    QByteArray opalExt;
    QByteArray thirdLayer;
    QByteArray thirdExt;
    const bool hybridTriple = Project::usesKyotoHybridTripleLayers();
    bool opalExtNeeded = QFile::exists(OpalTiles::extPath(metatiles_path));
    bool thirdExtNeeded = hybridTriple && QFile::exists(OpalTiles::thirdLayerExtPath(metatiles_path));
    QVector<QVector<int>> refs;
    const bool extended = !opalPaletteRefsPath.isEmpty();
    const int editorTiles = projectConfig.getNumTilesInMetatile();
    const int storedTiles = hybridTriple ? 8 : editorTiles;

    if (hybridTriple && editorTiles != 12) {
        logError(QString("Kyoto hybrid triple-layer authoring expected 12 editor subtiles, got %1").arg(editorTiles));
        return false;
    }

    for (const auto &metatile : m_metatiles) {
        QVector<int> row;
        for (int i = 0; i < editorTiles; i++) {
            Tile tile = metatile->tiles.value(i);
            if (tile.palette >= (extended ? 18 : 16)
                || (Project::getNumPalettesTotal() == 18 && !is_secondary && tile.palette >= 14)) {
                logError("Enable Opal extra palettes on the secondary tileset before using logical palette IDs 14–17.");
                return false;
            }
            row.append(tile.palette);
            if (extended && tile.palette >= 14) tile.palette = 0;

            int highBits = 0;
            if (Project::usesOpalExtendedTiles()) {
                int gameTile = OpalTiles::editorToGame(tile.tileId);
                highBits = (gameTile >> 10) << OpalTiles::ExtHighShift;
                tile.tileId = gameTile & 0x3FF;
            }

            const uint16_t raw = tile.rawValue();
            QByteArray *target = (hybridTriple && i >= storedTiles) ? &thirdLayer : &data;
            target->append(static_cast<char>(raw));
            target->append(static_cast<char>(raw >> 8));

            if (Project::usesOpalExtendedTiles()) {
                if (hybridTriple && i >= storedTiles) {
                    thirdExt.append(static_cast<char>(highBits));
                    thirdExtNeeded |= highBits != 0;
                } else {
                    opalExt.append(static_cast<char>(highBits));
                    opalExtNeeded |= highBits != 0;
                }
            }
        }
        refs.append(row);
    }

    QString error;
    if (extended && !OpalPalettes::writeReferences(opalPaletteRefsPath, refs, data, error)) {
        logError(error);
        return false;
    }
    if (Project::usesOpalExtendedTiles() && opalExtNeeded && !OpalTiles::writeExt(metatiles_path, opalExt, &error)) {
        logError(error);
        return false;
    }
    if (hybridTriple) {
        if (!OpalTiles::writeThirdLayer(metatiles_path, thirdLayer, &error)) {
            logError(error);
            return false;
        }
        if (thirdExtNeeded && !OpalTiles::writeThirdLayerExt(metatiles_path, thirdExt, &error)) {
            logError(error);
            return false;
        }
    }

    QSaveFile file(metatiles_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        logError(QString("Could not save %1: %2").arg(metatiles_path, file.errorString()));
        return false;
    }
    return true;
}'''

    text = tileset.read_text()
    pattern = r"bool Tileset::loadMetatiles\(\) \{.*?\n\}\n\nbool Tileset::saveMetatiles\(\) \{.*?\n\}\n\nbool Tileset::loadMetatileAttributes\(\)"
    replacement = new_load + "\n\n" + new_save + "\n\nbool Tileset::loadMetatileAttributes()"
    new_text, count = re.subn(pattern, lambda m: replacement, text, count=1, flags=re.S)
    if count != 1:
        if "Kyoto hybrid triple-layer authoring expected" in text:
            print("already patched: hybrid metatile load/save")
        else:
            raise SystemExit("src/core/tileset.cpp: could not replace metatile load/save functions")
    else:
        tileset.write_text(new_text)
        print("patched: hybrid metatile load/save")

    # 6) Layer-type UI: keep the selector visible and add Triple.
    editor = root / "src/ui/tileseteditor.cpp"
    old = '''    // Layer Type\n    if (!projectConfig.tripleLayerMetatilesEnabled) {\n        this->ui->comboBox_LayerType->addItem("Normal - Middle/Top",     Metatile::LayerType::Normal);\n        this->ui->comboBox_LayerType->addItem("Covered - Bottom/Middle", Metatile::LayerType::Covered);\n        this->ui->comboBox_LayerType->addItem("Split - Bottom/Top",      Metatile::LayerType::Split);\n        this->ui->comboBox_LayerType->setEditable(false);\n        this->ui->comboBox_LayerType->setMinimumContentsLength(0);\n        if (!projectConfig.metatileLayerTypeMask) {\n            // User doesn't have triple layer metatiles, but has no layer type attribute.\n            // Porymap is still using the layer type value to render metatiles, and with\n            // no mask set every metatile will be "Middle/Top", so just display the combo\n            // box but prevent the user from changing the value.\n            this->ui->comboBox_LayerType->setEnabled(false);\n        }\n    } else {\n        this->ui->frame_LayerType->setVisible(false);\n        this->ui->label_BottomTop->setText("Bottom/Middle/Top");\n    }'''
    new = '''    // Layer Type\n    if (Project::usesKyotoHybridTripleLayers()) {\n        this->ui->comboBox_LayerType->addItem("Normal - Middle/Top",      Metatile::LayerType::Normal);\n        this->ui->comboBox_LayerType->addItem("Covered - Bottom/Middle", Metatile::LayerType::Covered);\n        this->ui->comboBox_LayerType->addItem("Split - Bottom/Top",       Metatile::LayerType::Split);\n        this->ui->comboBox_LayerType->addItem("Triple - Bottom/Middle/Top", Metatile::LayerType::Triple);\n        this->ui->comboBox_LayerType->setEditable(false);\n        this->ui->comboBox_LayerType->setMinimumContentsLength(0);\n        this->ui->label_BottomTop->setText("Bottom/Middle/Top");\n    } else if (!projectConfig.tripleLayerMetatilesEnabled) {\n        this->ui->comboBox_LayerType->addItem("Normal - Middle/Top",     Metatile::LayerType::Normal);\n        this->ui->comboBox_LayerType->addItem("Covered - Bottom/Middle", Metatile::LayerType::Covered);\n        this->ui->comboBox_LayerType->addItem("Split - Bottom/Top",      Metatile::LayerType::Split);\n        this->ui->comboBox_LayerType->setEditable(false);\n        this->ui->comboBox_LayerType->setMinimumContentsLength(0);\n        if (!projectConfig.metatileLayerTypeMask) {\n            this->ui->comboBox_LayerType->setEnabled(false);\n        }\n    } else {\n        this->ui->frame_LayerType->setVisible(false);\n        this->ui->label_BottomTop->setText("Bottom/Middle/Top");\n    }'''
    replace_once(editor, old, new, "hybrid Triple layer selector")

    # 7) Renderer: only layer type 3 uses all 12 authored entries. Existing 0/1/2 remain visually identical.
    imageproviders = root / "src/ui/imageproviders.cpp"
    old = '''        if (projectConfig.tripleLayerMetatilesEnabled) {\n            tile = metatile->tiles.value(tileOffset + (layer * Metatile::tilesPerLayer()));\n        } else {\n            // "Vanilla" metatiles only have 8 tiles, but render 12.\n            // The remaining 4 tiles are rendered using user-specified tiles depending on layer type.\n            switch (layerType)\n            {\n            default:\n            case Metatile::LayerType::Normal:\n                if (layer == 0)\n                    tile = Tile(projectConfig.unusedTileNormal);\n                else // Tiles are on layers 1 and 2\n                    tile = metatile->tiles.value(tileOffset + ((layer - 1) * Metatile::tilesPerLayer()));\n                break;\n            case Metatile::LayerType::Covered:\n                if (layer == 2)\n                    tile = Tile(projectConfig.unusedTileCovered);\n                else // Tiles are on layers 0 and 1\n                    tile = metatile->tiles.value(tileOffset + (layer * Metatile::tilesPerLayer()));\n                break;\n            case Metatile::LayerType::Split:\n                if (layer == 1)\n                    tile = Tile(projectConfig.unusedTileSplit);\n                else // Tiles are on layers 0 and 2\n                    tile = metatile->tiles.value(tileOffset + ((layer == 0 ? 0 : 1) * Metatile::tilesPerLayer()));\n                break;\n            }\n        }'''
    new = '''        if (Project::usesKyotoHybridTripleLayers() && layerType == Metatile::LayerType::Triple) {\n            tile = metatile->tiles.value(tileOffset + (layer * Metatile::tilesPerLayer()));\n        } else if (Project::usesKyotoHybridTripleLayers() || !projectConfig.tripleLayerMetatilesEnabled) {\n            // Kyoto hybrid: layer types 0/1/2 keep the stock 8-subtile semantics.\n            switch (layerType)\n            {\n            default:\n            case Metatile::LayerType::Normal:\n                if (layer == 0)\n                    tile = Tile(projectConfig.unusedTileNormal);\n                else\n                    tile = metatile->tiles.value(tileOffset + ((layer - 1) * Metatile::tilesPerLayer()));\n                break;\n            case Metatile::LayerType::Covered:\n                if (layer == 2)\n                    tile = Tile(projectConfig.unusedTileCovered);\n                else\n                    tile = metatile->tiles.value(tileOffset + (layer * Metatile::tilesPerLayer()));\n                break;\n            case Metatile::LayerType::Split:\n                if (layer == 1)\n                    tile = Tile(projectConfig.unusedTileSplit);\n                else\n                    tile = metatile->tiles.value(tileOffset + ((layer == 0 ? 0 : 1) * Metatile::tilesPerLayer()));\n                break;\n            }\n        } else {\n            tile = metatile->tiles.value(tileOffset + (layer * Metatile::tilesPerLayer()));\n        }'''
    replace_once(imageproviders, old, new, "hybrid triple renderer")

    print("\nPASS: Kyoto hybrid triple-layer Porymap source patch installed.")

if __name__ == "__main__":
    main()
