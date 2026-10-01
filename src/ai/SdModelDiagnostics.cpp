#include "SdModelDiagnostics.h"
#include "core/MatUtil.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QCryptographicHash>
#include <algorithm>
#include <cstring>

namespace Sd {
namespace {

// ============================================================================================ heuristique d'architecture (commune)
QString archGuessFromMarkers(bool unet, bool vae, bool clip, bool sdxlCond, bool flux, bool sd3, bool t5) {
    QStringList found;
    if (unet) found << "UNet (style SD 1.x/2.x/SDXL)";
    if (flux) found << "blocs de diffusion de type Flux";
    if (sd3) found << "blocs de diffusion de type SD3";
    if (vae) found << "VAE";
    if (clip) found << "encodeur de texte CLIP";
    if (sdxlCond) found << "second encodeur de texte (style SDXL)";
    if (t5) found << "encodeur de texte T5";
    if (found.isEmpty()) return {};

    QString v = "Éléments détectés : " + found.join(", ") + ". ";
    const bool hasDiffusion = unet || flux || sd3;
    const bool hasAnyText = clip || sdxlCond || t5;
    if (hasDiffusion && vae && hasAnyText)
        v += "Cela ressemble à un checkpoint complet autonome : remplir uniquement « Checkpoint complet » devrait suffire.";
    else if (hasDiffusion && !hasAnyText)
        v += "Cela ressemble à un modèle de diffusion SEUL, sans encodeur de texte intégré : utilisez l'onglet « Modèle de diffusion + encodeurs », "
             "avec l'encodeur (ou les encodeurs) de texte correspondant renseigné séparément.";
    else if (hasAnyText && !hasDiffusion && !vae)
        v += "Cela ressemble à un encodeur de texte seul (CLIP/T5) : à renseigner dans l'onglet « Modèle de diffusion + encodeurs », "
             "pas comme checkpoint complet.";
    else if (vae && !hasDiffusion && !hasAnyText)
        v += "Cela ressemble à un VAE seul : à renseigner dans le champ « VAE », pas comme checkpoint complet.";
    else
        v += "Combinaison partielle : vérifiez qu'il ne manque pas une pièce (VAE ou encodeur de texte) pour ce fichier précis.";
    return v;
}

QString archGuessFromNames(const QStringList& names) {
    auto has = [&](const char* needle) { for (const QString& n : names) if (n.contains(needle)) return true; return false; };
    return archGuessFromMarkers(has("diffusion_model.input_blocks") || has("diffusion_model.output_blocks"),
                                 has("first_stage_model.") || has("vae."),
                                 has("cond_stage_model.") || has("text_model.") || has("transformer.text_model"),
                                 has("conditioner.embedders"), has("double_blocks."), has("joint_blocks."),
                                 has("t5xxl") || has(".t5.") || has("text_encoders.t5"));
}

QString archGuessFromRawBytes(const QByteArray& b) {
    auto has = [&](const char* needle) { return b.contains(needle); };
    return archGuessFromMarkers(has("diffusion_model.input_blocks") || has("diffusion_model.output_blocks"),
                                 has("first_stage_model."), has("cond_stage_model.") || has("transformer.text_model"),
                                 has("conditioner.embedders"), has("double_blocks."), has("joint_blocks."),
                                 has("t5xxl"));
}

// ============================================================================================ GGUF
// Spécification : https://github.com/ggml-org/ggml/blob/master/docs/gguf.md — implémentation minimale, en LECTURE
// SEULE et séquentielle (jamais de saut à un décalage non vérifié), pour ne jamais planter sur un fichier corrompu.
enum GgufType { G_U8 = 0, G_I8, G_U16, G_I16, G_U32, G_I32, G_F32, G_BOOL, G_STRING, G_ARRAY, G_U64, G_I64, G_F64 };

bool ggufReadString(QFile& f, QString* out) {
    quint64 len = 0;
    if (f.read(reinterpret_cast<char*>(&len), 8) != 8) return false;
    if (len > (1ull << 30)) return false;               // garde-fou : chaîne d'1 Go+ = fichier aberrant, pas une vraie chaîne
    const QByteArray b = f.read(qint64(len));
    if (quint64(b.size()) != len) return false;
    if (out) *out = QString::fromUtf8(b);
    return true;
}

qint64 ggufFixedSize(quint32 type) {
    switch (type) {
    case G_U8: case G_I8: case G_BOOL: return 1;
    case G_U16: case G_I16: return 2;
    case G_U32: case G_I32: case G_F32: return 4;
    case G_U64: case G_I64: case G_F64: return 8;
    default: return -1;                                 // STRING / ARRAY : taille variable
    }
}

// Lit une valeur GGUF de type `type` et avance le fichier de la bonne quantité — y compris pour les types qu'on
// n'affiche pas (tableaux…), ce qui est ce qui permet de continuer à parser le reste du fichier sans décalage.
bool ggufReadValue(QFile& f, quint32 type, QString* asText, int depth = 0) {
    if (const qint64 fixed = ggufFixedSize(type); fixed > 0) {
        const QByteArray b = f.read(fixed);
        if (b.size() != fixed) return false;
        if (asText) {
            switch (type) {
            case G_U8: *asText = QString::number(quint8(b[0])); break;
            case G_I8: *asText = QString::number(qint8(b[0])); break;
            case G_BOOL: *asText = (b[0] != 0) ? "vrai" : "faux"; break;
            case G_U16: { quint16 v; memcpy(&v, b.constData(), 2); *asText = QString::number(v); break; }
            case G_I16: { qint16 v; memcpy(&v, b.constData(), 2); *asText = QString::number(v); break; }
            case G_U32: { quint32 v; memcpy(&v, b.constData(), 4); *asText = QString::number(v); break; }
            case G_I32: { qint32 v; memcpy(&v, b.constData(), 4); *asText = QString::number(v); break; }
            case G_F32: { float v; memcpy(&v, b.constData(), 4); *asText = QString::number(double(v), 'g', 6); break; }
            case G_U64: { quint64 v; memcpy(&v, b.constData(), 8); *asText = QString::number(v); break; }
            case G_I64: { qint64 v; memcpy(&v, b.constData(), 8); *asText = QString::number(v); break; }
            case G_F64: { double v; memcpy(&v, b.constData(), 8); *asText = QString::number(v, 'g', 10); break; }
            default: break;
            }
        }
        return true;
    }
    if (type == G_STRING) {
        QString s;
        if (!ggufReadString(f, &s)) return false;
        if (asText) *asText = s;
        return true;
    }
    if (type == G_ARRAY) {
        if (depth >= 8) return false;                    // garde-fou : des tableaux imbriqués sans fin épuiseraient la pile (vrais fichiers : 1 niveau)
        quint32 subtype = 0;
        quint64 count = 0;
        if (f.read(reinterpret_cast<char*>(&subtype), 4) != 4) return false;
        if (f.read(reinterpret_cast<char*>(&count), 8) != 8) return false;
        if (count > 5'000'000ull) return false;          // garde-fou
        for (quint64 i = 0; i < count; ++i)
            if (!ggufReadValue(f, subtype, nullptr, depth + 1)) return false;
        if (asText) *asText = QString("[tableau de %1 élément(s)]").arg(count);
        return true;
    }
    return false;                                        // type inconnu : impossible de savoir de combien avancer
}

void readGguf(QFile& f, ModelDiagnostic& d) {
    d.format = "GGUF";
    f.seek(4);                                            // signature déjà vérifiée par l'appelant
    quint32 version = 0;
    quint64 nTensors = 0, nKv = 0;
    if (f.read(reinterpret_cast<char*>(&version), 4) != 4) { d.issues << "en-tête GGUF tronqué (version)."; return; }
    if (f.read(reinterpret_cast<char*>(&nTensors), 8) != 8) { d.issues << "en-tête GGUF tronqué (nombre de tenseurs)."; return; }
    if (f.read(reinterpret_cast<char*>(&nKv), 8) != 8) { d.issues << "en-tête GGUF tronqué (nombre de métadonnées)."; return; }
    if (nTensors > 1'000'000ull || nKv > 1'000'000ull) {
        d.issues << "en-tête GGUF invraisemblable (nombre de tenseurs ou de métadonnées aberrant) : fichier probablement corrompu.";
        return;
    }
    d.metadata << QString("Version du format GGUF : %1").arg(version);

    for (quint64 i = 0; i < nKv; ++i) {
        QString key;
        if (!ggufReadString(f, &key)) { d.issues << QString("métadonnées GGUF tronquées (entrée %1 sur %2).").arg(i + 1).arg(nKv); return; }
        quint32 type = 0;
        if (f.read(reinterpret_cast<char*>(&type), 4) != 4) { d.issues << "métadonnées GGUF tronquées (type de valeur)."; return; }
        QString val;
        if (!ggufReadValue(f, type, &val)) { d.issues << QString("métadonnées GGUF tronquées (valeur de « %1 »).").arg(key); return; }
        if (d.metadata.size() < 40) d.metadata << QString("%1 : %2").arg(key, val);
    }

    QStringList names;
    for (quint64 i = 0; i < nTensors; ++i) {
        QString name;
        if (!ggufReadString(f, &name)) { d.issues << QString("liste des tenseurs GGUF tronquée (entrée %1 sur %2).").arg(i + 1).arg(nTensors); return; }
        quint32 nDims = 0;
        if (f.read(reinterpret_cast<char*>(&nDims), 4) != 4 || nDims > 8) { d.issues << QString("dimensions invalides pour le tenseur « %1 ».").arg(name); return; }
        for (quint32 k = 0; k < nDims; ++k) {
            quint64 dim = 0;
            if (f.read(reinterpret_cast<char*>(&dim), 8) != 8) { d.issues << "liste des tenseurs GGUF tronquée (dimensions)."; return; }
        }
        quint32 ggmlType = 0;
        quint64 offset = 0;
        if (f.read(reinterpret_cast<char*>(&ggmlType), 4) != 4 || f.read(reinterpret_cast<char*>(&offset), 8) != 8) {
            d.issues << "liste des tenseurs GGUF tronquée (type/décalage)."; return;
        }
        names << name;
    }
    d.tensorCount = qint64(nTensors);
    if (names.size() > 30) { d.sampleNames = names.mid(0, 30); d.sampleNames << QString("… et %1 de plus").arg(names.size() - 30); }
    else d.sampleNames = names;

    const qint64 dataStart = f.pos();
    if (dataStart > d.sizeBytes) { d.issues << "le fichier s'arrête avant la fin de la liste des tenseurs : tronqué."; return; }
    if (nTensors > 0 && (d.sizeBytes - dataStart) < qint64(nTensors) * 64)
        d.issues << "les données des tenseurs semblent beaucoup plus petites que ce que la liste laisse attendre : fichier probablement tronqué.";
    d.structurallyValid = true;
    d.archGuess = archGuessFromNames(names);
}

// ============================================================================================ safetensors
void readSafetensors(QFile& f, ModelDiagnostic& d) {
    d.format = "Safetensors";
    f.seek(0);
    quint64 headerLen = 0;
    if (f.read(reinterpret_cast<char*>(&headerLen), 8) != 8) { d.issues << "en-tête safetensors tronqué."; return; }
    if (headerLen == 0 || headerLen > (256ull << 20)) { d.issues << "taille d'en-tête safetensors invraisemblable : fichier probablement corrompu."; return; }
    if (8 + qint64(headerLen) > d.sizeBytes) { d.issues << "l'en-tête déclaré est plus grand que le fichier lui-même : fichier tronqué."; return; }
    const QByteArray headerBytes = f.read(qint64(headerLen));
    if (quint64(headerBytes.size()) != headerLen) { d.issues << "lecture de l'en-tête safetensors interrompue (fichier tronqué)."; return; }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(headerBytes, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        d.issues << QString("en-tête safetensors : JSON invalide (%1) — fichier corrompu.").arg(perr.errorString());
        return;
    }
    const QJsonObject obj = doc.object();
    qint64 maxEnd = 0;
    QStringList names;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it.key() == "__metadata__") {
            if (it.value().isObject()) {
                const QJsonObject meta = it.value().toObject();
                for (auto mit = meta.begin(); mit != meta.end(); ++mit)
                    if (d.metadata.size() < 40) d.metadata << QString("%1 : %2").arg(mit.key(), mit.value().toString());
            }
            continue;
        }
        names << it.key();
        if (it.value().isObject()) {
            const QJsonArray offs = it.value().toObject().value("data_offsets").toArray();
            if (offs.size() == 2) maxEnd = std::max(maxEnd, qint64(offs[1].toDouble()));
        }
    }
    names.sort();
    d.tensorCount = names.size();
    if (names.size() > 30) { d.sampleNames = names.mid(0, 30); d.sampleNames << QString("… et %1 de plus").arg(names.size() - 30); }
    else d.sampleNames = names;

    const qint64 expected = 8 + qint64(headerLen) + maxEnd;
    if (expected > d.sizeBytes)
        d.issues << QString("taille du fichier incohérente avec les décalages déclarés dans l'en-tête (attendu au moins %1, fichier de %2) : probablement tronqué.")
                         .arg(mu::humanSize(expected), mu::humanSize(d.sizeBytes));
    d.structurallyValid = true;
    d.archGuess = archGuessFromNames(names);
}

// ============================================================================================ archive zip PyTorch (.ckpt / .pt)
// Lecteur minimal, en lecture seule, focalisé sur la QUEUE du fichier (là où se trouve la table centrale zip) : rapide
// même sur un fichier de plusieurs Go, puisqu'on ne lit jamais tout le fichier pour lister ses entrées. Gère le
// Zip64 (obligatoire au-delà de 4 Go, le cas normal pour un checkpoint de diffusion complet).
struct ZipEntry { QString name; quint16 method; quint64 compSize, uncompSize; quint64 localOffset; };

bool zipReadCentralDirectory(QFile& f, std::vector<ZipEntry>* entries, QString* err) {
    const qint64 fsize = f.size();
    const qint64 tailLen = std::min<qint64>(fsize, 22 + 65536);   // EOCD (22 o) + commentaire max (64 Kio)
    f.seek(fsize - tailLen);
    const QByteArray tail = f.read(tailLen);
    if (tail.size() != tailLen) { *err = "lecture de la fin du fichier impossible."; return false; }

    int eocdPos = -1;
    for (int i = int(tail.size()) - 22; i >= 0; --i) {
        if (memcmp(tail.constData() + i, "\x50\x4b\x05\x06", 4) == 0) { eocdPos = i; break; }
    }
    if (eocdPos < 0) {
        *err = "aucune fin d'archive zip (« End Of Central Directory ») trouvée dans les derniers kilo-octets du fichier : "
               "le fichier est presque certainement tronqué (téléchargement interrompu).";
        return false;
    }
    auto u16 = [&](int off) { quint16 v; memcpy(&v, tail.constData() + off, 2); return v; };
    auto u32 = [&](int off) { quint32 v; memcpy(&v, tail.constData() + off, 4); return v; };

    quint64 entryCount = u16(eocdPos + 10);
    quint64 cdSize = u32(eocdPos + 12);
    quint64 cdOffset = u32(eocdPos + 16);

    // Zip64 : repère le localisateur (20 octets, juste avant l'EOCD classique) puis l'enregistrement Zip64 lui-même.
    const bool needsZip64 = (entryCount == 0xFFFF) || (cdSize == 0xFFFFFFFFu) || (cdOffset == 0xFFFFFFFFu);
    if (needsZip64) {
        const int locPos = eocdPos - 20;
        if (locPos < 0 || memcmp(tail.constData() + locPos, "\x50\x4b\x06\x07", 4) != 0) {
            *err = "archive Zip64 attendue (fichier de plus de 4 Go) mais le localisateur Zip64 est absent ou corrompu.";
            return false;
        }
        quint64 z64EocdOffset;
        memcpy(&z64EocdOffset, tail.constData() + locPos + 8, 8);
        f.seek(qint64(z64EocdOffset));
        const QByteArray rec = f.read(56);
        if (rec.size() != 56 || memcmp(rec.constData(), "\x50\x4b\x06\x06", 4) != 0) {
            *err = "enregistrement Zip64 de fin de table centrale illisible ou corrompu.";
            return false;
        }
        memcpy(&entryCount, rec.constData() + 32, 8);
        memcpy(&cdSize, rec.constData() + 40, 8);
        memcpy(&cdOffset, rec.constData() + 48, 8);
    }
    if (cdOffset > quint64(fsize) || cdSize > quint64(fsize) - cdOffset) {   // écrit sans addition : cdOffset + cdSize peut déborder (Zip64 forgé)
        *err = "la table centrale déclarée dépasse la taille du fichier : archive tronquée ou corrompue.";
        return false;
    }
    if (entryCount > 2'000'000ull) { *err = "nombre d'entrées invraisemblable : archive corrompue."; return false; }
    if (cdSize > (256ull << 20)) { *err = "table centrale invraisemblablement grande : archive corrompue."; return false; }   // garde-fou mémoire : quelques centaines de Kio en pratique

    f.seek(qint64(cdOffset));
    const QByteArray cd = f.read(qint64(cdSize));
    if (quint64(cd.size()) != cdSize) { *err = "lecture de la table centrale interrompue (fichier tronqué)."; return false; }

    qint64 p = 0;
    for (quint64 i = 0; i < entryCount; ++i) {
        if (p + 46 > cd.size() || memcmp(cd.constData() + p, "\x50\x4b\x01\x02", 4) != 0) {
            *err = QString("table centrale corrompue à l'entrée %1 sur %2.").arg(i + 1).arg(entryCount);
            return false;
        }
        auto cu16 = [&](int off) { quint16 v; memcpy(&v, cd.constData() + p + off, 2); return v; };
        auto cu32 = [&](int off) { quint32 v; memcpy(&v, cd.constData() + p + off, 4); return v; };
        const quint16 method = cu16(10);
        quint64 compSize = cu32(20), uncompSize = cu32(24), localOffset = cu32(42);
        const quint16 nameLen = cu16(28), extraLen = cu16(30), commentLen = cu16(32);
        if (p + 46 + nameLen + extraLen + commentLen > cd.size()) { *err = "table centrale corrompue (entrée tronquée)."; return false; }
        const QString name = QString::fromUtf8(cd.constData() + p + 46, nameLen);

        if (compSize == 0xFFFFFFFFu || uncompSize == 0xFFFFFFFFu || localOffset == 0xFFFFFFFFu) {
            int ep = 0;
            const char* extra = cd.constData() + p + 46 + nameLen;
            while (ep + 4 <= extraLen) {
                quint16 id, sz;
                memcpy(&id, extra + ep, 2);
                memcpy(&sz, extra + ep + 2, 2);
                if (id == 0x0001 && ep + 4 + sz <= extraLen) {
                    int fp = ep + 4;
                    if (uncompSize == 0xFFFFFFFFu && fp + 8 <= ep + 4 + sz) { memcpy(&uncompSize, extra + fp, 8); fp += 8; }
                    if (compSize == 0xFFFFFFFFu && fp + 8 <= ep + 4 + sz) { memcpy(&compSize, extra + fp, 8); fp += 8; }
                    if (localOffset == 0xFFFFFFFFu && fp + 8 <= ep + 4 + sz) { memcpy(&localOffset, extra + fp, 8); fp += 8; }
                    break;
                }
                ep += 4 + sz;
            }
        }
        entries->push_back({name, method, compSize, uncompSize, localOffset});
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

// Lit le contenu brut (stocké, non compressé) d'une entrée en repartant de son en-tête LOCAL (dont les longueurs de
// nom/champ-extra peuvent différer de la copie de la table centrale) — capé pour rester rapide et borné en mémoire.
QByteArray zipReadStoredEntry(QFile& f, const ZipEntry& e, qint64 cap) {
    f.seek(qint64(e.localOffset));
    const QByteArray lh = f.read(30);
    if (lh.size() != 30 || memcmp(lh.constData(), "\x50\x4b\x03\x04", 4) != 0) return {};
    quint16 nameLen, extraLen;
    memcpy(&nameLen, lh.constData() + 26, 2);
    memcpy(&extraLen, lh.constData() + 28, 2);
    f.seek(qint64(e.localOffset) + 30 + nameLen + extraLen);
    return f.read(std::min<qint64>(qint64(e.uncompSize), cap));
}

void readTorchZip(QFile& f, ModelDiagnostic& d) {
    d.format = "Archive zip PyTorch (.ckpt/.pt)";
    std::vector<ZipEntry> entries;
    QString err;
    if (!zipReadCentralDirectory(f, &entries, &err)) { d.issues << err; return; }
    if (entries.empty()) { d.issues << "l'archive ne contient aucune entrée."; return; }

    QStringList names;
    quint64 totalUncomp = 0;
    bool hasDataPkl = false, hasVersion = false;
    int dataPklIndex = -1;
    for (size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& e = entries[i];
        names << e.name;
        totalUncomp += e.uncompSize;
        if (e.name.endsWith("/data.pkl") || e.name == "data.pkl") { hasDataPkl = true; dataPklIndex = int(i); }
        if (e.name.endsWith("/version") || e.name == "version") hasVersion = true;
    }
    d.tensorCount = qint64(entries.size());               // nb d'entrées zip (≈ nb de tenseurs + quelques fichiers annexes)
    if (names.size() > 20) { d.sampleNames = names.mid(0, 20); d.sampleNames << QString("… et %1 de plus").arg(names.size() - 20); }
    else d.sampleNames = names;
    d.metadata << QString("%1 entrée(s) dans l'archive, %2 au total (non compressé).").arg(entries.size()).arg(mu::humanSize(qint64(totalUncomp)));
    if (!hasDataPkl) d.issues << "aucune entrée « data.pkl » trouvée : ceci ne ressemble pas à une archive PyTorch standard (torch.save).";
    if (!hasVersion) d.metadata << "(pas d'entrée « version » — format zip PyTorch ancien ou non standard, cela reste possible)";
    d.structurallyValid = hasDataPkl;

    if (dataPklIndex >= 0) {
        const ZipEntry& pkl = entries[size_t(dataPklIndex)];
        if (pkl.method != 0) {
            d.metadata << "le contenu de data.pkl est compressé : estimation de l'architecture impossible (seule la structure de l'archive a été vérifiée).";
        } else {
            const QByteArray content = zipReadStoredEntry(f, pkl, 64ll << 20);   // 64 Mio : très large pour un simple state_dict pickle
            if (content.isEmpty()) {
                d.issues << "impossible de relire le contenu de data.pkl (décalage local incohérent) : archive corrompue.";
            } else {
                d.archGuess = archGuessFromRawBytes(content);
            }
        }
    }
}

}   // namespace

// ============================================================================================ point d'entrée
ModelDiagnostic diagnoseModelFile(const QString& path, bool computeSha256) {
    ModelDiagnostic d;
    const QFileInfo fi(path);
    d.exists = fi.exists() && fi.isFile();
    if (!d.exists) { d.format = "Inconnu"; d.issues << "fichier introuvable."; return d; }
    d.sizeBytes = fi.size();

    QFile f(path);
    d.readable = f.open(QIODevice::ReadOnly);
    if (!d.readable) { d.format = "Inconnu"; d.issues << "fichier illisible (droits d'accès ?)."; return d; }
    if (d.sizeBytes < 8) { d.format = "Inconnu"; d.issues << "fichier vide ou trop petit pour être un modèle."; return d; }

    QByteArray head = f.read(16);
    if (head.startsWith("GGUF")) {
        readGguf(f, d);
    } else if (head.startsWith("PK\x03\x04")) {
        readTorchZip(f, d);
    } else if (static_cast<uchar>(head[0]) == 0x80) {
        d.format = "Pickle brut (ancien .ckpt)";
        d.metadata << "Ancien format de sérialisation PyTorch (pré-zip). Structure interne non analysée en détail ici ; "
                       "la bibliothèque saura le charger si le fichier n'est pas tronqué.";
        d.structurallyValid = true;   // on ne peut pas en dire beaucoup plus sans un dépaqueteur pickle complet
    } else {
        quint64 n = 0;
        for (int i = 7; i >= 0; --i) n = (n << 8) | static_cast<uchar>(head[i]);
        f.seek(8);
        const bool looksLikeSafetensors = n > 0 && n < (256ull << 20) && 8 + qint64(n) <= d.sizeBytes && f.read(1) == "{";
        if (looksLikeSafetensors) readSafetensors(f, d);
        else { d.format = "Inconnu"; d.issues << "signature de fichier non reconnue (ni GGUF, ni safetensors, ni archive zip PyTorch, ni pickle)."; }
    }

    if (computeSha256) {
        f.seek(0);
        QCryptographicHash h(QCryptographicHash::Sha256);
        QByteArray buf;
        buf.resize(4 << 20);
        while (true) {
            const qint64 n = f.read(buf.data(), buf.size());
            if (n <= 0) break;
            h.addData(buf.constData(), int(n));
        }
        d.sha256 = QString::fromLatin1(h.result().toHex());
    }
    return d;
}

QString formatDiagnostic(const ModelDiagnostic& d) {
    QStringList lines;
    if (!d.exists) return "Fichier introuvable.";
    lines << QString("Taille : %1 (%2 octets)").arg(mu::humanSize(d.sizeBytes)).arg(d.sizeBytes);
    lines << QString("Format détecté : %1").arg(d.format.isEmpty() ? "inconnu" : d.format);
    lines << QString("Structure : %1").arg(d.structurallyValid ? "cohérente (le conteneur a pu être analysé en entier)" : "PROBLÈME DÉTECTÉ (voir ci-dessous)");
    if (d.tensorCount >= 0) lines << QString("Nombre de tenseurs/entrées : %1").arg(d.tensorCount);
    if (!d.sha256.isEmpty()) lines << QString("SHA-256 : %1").arg(d.sha256);
    if (!d.issues.isEmpty()) {
        lines << "" << "⚠ Problèmes détectés :";
        for (const QString& s : d.issues) lines << " • " + s;
    }
    if (!d.archGuess.isEmpty()) lines << "" << "Interprétation (heuristique, non garantie) :" << d.archGuess;
    if (!d.metadata.isEmpty()) {
        lines << "" << "Métadonnées :";
        for (const QString& s : d.metadata) lines << " • " + s;
    }
    if (!d.sampleNames.isEmpty()) {
        lines << "" << QString("Aperçu des noms (%1 affiché(s)) :").arg(d.sampleNames.size());
        for (const QString& s : d.sampleNames) lines << " • " + s;
    }
    return lines.join("\n");
}

}   // namespace Sd
