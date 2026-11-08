// of_render : applique l'égaliseur Open Filter à un fichier WAV, avec exactement le même code C++ que le plugin.
// Sert à écouter et à mesurer sans ouvrir Live. Exemples :
//   of_render in.wav out.wav --band bell,1000,6,1
//   of_render gen:sweep out.wav --band lowcut,80,0,0.71,24 --band highshelf,8000,3,0.71,12,side --gain -2
//   of_render gen:pink out.wav --band bell,250,-4,2 --response courbe.csv
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "dsp/EqEngine.h"
#include "dsp/FilterDesign.h"

using namespace openfilter::dsp;

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    struct Audio
    {
        double sampleRate = 48000.0;
        int channels = 2;
        std::vector<std::vector<float>> ch; // [canal][échantillon]
    };

    std::uint32_t rd32 (const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((std::uint32_t) p[3] << 24); }
    std::uint16_t rd16 (const unsigned char* p) { return (std::uint16_t) (p[0] | (p[1] << 8)); }

    bool readWav (const std::string& path, Audio& a, std::string& err)
    {
        std::ifstream f (path, std::ios::binary);

        if (! f) { err = "fichier introuvable : " + path; return false; }

        std::vector<unsigned char> d ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());

        if (d.size() < 44 || std::memcmp (d.data(), "RIFF", 4) != 0 || std::memcmp (d.data() + 8, "WAVE", 4) != 0)
        {
            err = "ce n'est pas un fichier WAV";
            return false;
        }

        int format = 0, channels = 0, bits = 0;
        std::uint32_t rate = 0;
        const unsigned char* data = nullptr;
        std::uint32_t dataSize = 0;

        for (size_t pos = 12; pos + 8 <= d.size();)
        {
            const std::uint32_t size = rd32 (&d[pos + 4]);
            const unsigned char* body = &d[pos + 8];

            if (std::memcmp (&d[pos], "fmt ", 4) == 0 && size >= 16)
            {
                format = rd16 (body); channels = rd16 (body + 2); rate = rd32 (body + 4); bits = rd16 (body + 14);

                if (format == 0xFFFE && size >= 26) // WAVE_FORMAT_EXTENSIBLE : le vrai format est dans le sous-type
                    format = rd16 (body + 24);
            }
            else if (std::memcmp (&d[pos], "data", 4) == 0)
            {
                data = body;
                dataSize = (std::uint32_t) std::min<size_t> (size, d.size() - pos - 8);
            }

            pos += 8 + (size_t) size + (size & 1u);
        }

        if (data == nullptr || channels < 1 || channels > 2)
        {
            err = "format non géré (mono ou stéréo, PCM 16/24/32 bits ou float 32 bits)";
            return false;
        }

        const bool isFloat = format == 3 && bits == 32;
        const bool isPcm = format == 1 && (bits == 16 || bits == 24 || bits == 32);

        if (! isFloat && ! isPcm)
        {
            err = "format non géré (PCM 16/24/32 bits ou float 32 bits)";
            return false;
        }

        const int bytes = bits / 8;
        const size_t frames = dataSize / (size_t) (bytes * channels);
        a.sampleRate = rate;
        a.channels = channels;
        a.ch.assign ((size_t) channels, std::vector<float> (frames));

        for (size_t i = 0; i < frames; ++i)
            for (int c = 0; c < channels; ++c)
            {
                const unsigned char* p = data + (i * (size_t) channels + (size_t) c) * (size_t) bytes;
                float v;

                if (isFloat)           { std::uint32_t u = rd32 (p); std::memcpy (&v, &u, 4); }
                else if (bits == 16)   v = (float) (std::int16_t) rd16 (p) / 32768.0f;
                else if (bits == 24)   v = (float) ((std::int32_t) ((p[0] << 8) | (p[1] << 16) | ((std::uint32_t) p[2] << 24)) >> 8) / 8388608.0f;
                else                   v = (float) ((std::int32_t) rd32 (p)) / 2147483648.0f;

                a.ch[(size_t) c][i] = v;
            }

        return true;
    }

    void wr32 (std::vector<unsigned char>& o, std::uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back ((unsigned char) (v >> (8 * i))); }
    void wr16 (std::vector<unsigned char>& o, std::uint16_t v) { o.push_back ((unsigned char) v); o.push_back ((unsigned char) (v >> 8)); }

    // WAV float 32 bits : pas d'écrêtage, on peut mesurer les dépassements.
    bool writeWav (const std::string& path, const Audio& a)
    {
        const size_t frames = a.ch.empty() ? 0 : a.ch[0].size();
        const std::uint32_t dataSize = (std::uint32_t) (frames * (size_t) a.channels * 4);
        std::vector<unsigned char> o;
        o.insert (o.end(), { 'R', 'I', 'F', 'F' }); wr32 (o, 36 + dataSize);
        o.insert (o.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' }); wr32 (o, 16);
        wr16 (o, 3); wr16 (o, (std::uint16_t) a.channels); wr32 (o, (std::uint32_t) a.sampleRate);
        wr32 (o, (std::uint32_t) (a.sampleRate * a.channels * 4)); wr16 (o, (std::uint16_t) (a.channels * 4)); wr16 (o, 32);
        o.insert (o.end(), { 'd', 'a', 't', 'a' }); wr32 (o, dataSize);

        for (size_t i = 0; i < frames; ++i)
            for (int c = 0; c < a.channels; ++c)
            {
                std::uint32_t u;
                std::memcpy (&u, &a.ch[(size_t) c][i], 4);
                wr32 (o, u);
            }

        std::ofstream f (path, std::ios::binary);
        f.write (reinterpret_cast<const char*> (o.data()), (std::streamsize) o.size());
        return (bool) f;
    }

    // Signaux de test intégrés : « gen:sweep » (balayage 20 Hz - 20 kHz, 8 s) et « gen:pink » (bruit rose, 10 s).
    bool generate (const std::string& what, double fs, Audio& a)
    {
        a.sampleRate = fs;
        a.channels = 2;
        std::uint64_t s = 12345;
        const auto rnd = [&] { s = s * 6364136223846793005ull + 1442695040888963407ull; return (double) (std::int64_t) (s >> 11) / (double) (1ull << 52) - 1.0; };

        if (what == "sweep")
        {
            const size_t n = (size_t) (8.0 * fs);
            a.ch.assign (2, std::vector<float> (n));
            const double k = std::log (20000.0 / 20.0), T = 8.0;

            for (size_t i = 0; i < n; ++i)
            {
                const double t = (double) i / fs;
                const double phase = 2.0 * kPi * 20.0 * T / k * (std::exp (t / T * k) - 1.0);
                a.ch[0][i] = a.ch[1][i] = (float) (0.25 * std::sin (phase));
            }
            return true;
        }

        if (what == "pink")
        {
            const size_t n = (size_t) (10.0 * fs);
            a.ch.assign (2, std::vector<float> (n));

            for (int c = 0; c < 2; ++c)
            {
                double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0; // filtre de Paul Kellet

                for (size_t i = 0; i < n; ++i)
                {
                    const double w = rnd();
                    b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
                    b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
                    a.ch[(size_t) c][i] = (float) (0.12 * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362));
                    b6 = w * 0.115926;
                }
            }
            return true;
        }

        return false;
    }

    bool parseType (const std::string& t, FilterType& out)
    {
        static const struct { const char* name; FilterType type; } names[] = {
            { "bell", FilterType::Bell }, { "lowshelf", FilterType::LowShelf }, { "highshelf", FilterType::HighShelf },
            { "lowcut", FilterType::LowCut }, { "highcut", FilterType::HighCut }, { "notch", FilterType::Notch },
            { "bandpass", FilterType::BandPass } };

        for (const auto& n : names)
            if (t == n.name) { out = n.type; return true; }
        return false;
    }

    bool parsePlacement (const std::string& t, Placement& out)
    {
        static const struct { const char* name; Placement p; } names[] = {
            { "stereo", Placement::Stereo }, { "left", Placement::Left }, { "right", Placement::Right },
            { "mid", Placement::Mid }, { "side", Placement::Side } };

        for (const auto& n : names)
            if (t == n.name) { out = n.p; return true; }
        return false;
    }

    // « type,freq,gain,q[,pente[,placement]] »
    bool parseBand (const std::string& text, BandSettings& b, std::string& err)
    {
        std::vector<std::string> p;
        size_t start = 0;

        for (;;)
        {
            const size_t comma = text.find (',', start);
            p.push_back (text.substr (start, comma == std::string::npos ? std::string::npos : comma - start));

            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        if (p.size() < 4 || p.size() > 6 || ! parseType (p[0], b.filter.type))
        {
            err = "bande illisible : « " + text + " » (attendu type,freq,gain,q[,pente[,placement]] ; types : bell lowshelf highshelf lowcut highcut notch bandpass)";
            return false;
        }

        b.used = true;
        b.on = true;
        b.filter.freq = std::atof (p[1].c_str());
        b.filter.gainDb = std::atof (p[2].c_str());
        b.filter.q = std::atof (p[3].c_str());
        b.filter.slopeDb = p.size() > 4 ? std::atoi (p[4].c_str()) : 12;
        b.placement = Placement::Stereo;

        if (p.size() > 5 && ! parsePlacement (p[5], b.placement))
        {
            err = "placement inconnu : " + p[5] + " (stereo left right mid side)";
            return false;
        }
        return true;
    }

    void usage()
    {
        std::puts ("usage : of_render <entrée.wav | gen:sweep | gen:pink> <sortie.wav> [options]\n"
                   "  --band type,freq,gain,q[,pente[,placement]]   (répétable, jusqu'à 24 bandes)\n"
                   "  --gain <dB>          gain de sortie\n"
                   "  --invert             inverse la phase\n"
                   "  --bypass             sortie = entrée\n"
                   "  --block <n>          taille de bloc simulée (défaut 512)\n"
                   "  --response <fichier.csv>   écrit la courbe de réponse (fréquence, dB numérique, dB analogique)\n"
                   "types : bell lowshelf highshelf lowcut highcut notch bandpass ; placements : stereo left right mid side");
    }
}

int main (int argc, char** argv)
{
    if (argc < 3) { usage(); return 1; }

    std::vector<BandSettings> bands;
    double gainDb = 0.0;
    bool invert = false, bypass = false;
    int block = 512;
    std::string responsePath;

    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&] () -> const char* { return i + 1 < argc ? argv[++i] : ""; };

        if (a == "--band")
        {
            BandSettings b;
            std::string err;

            if (! parseBand (next(), b, err)) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
            if ((int) bands.size() >= EqEngine::kNumBands) { std::fprintf (stderr, "24 bandes au maximum\n"); return 1; }
            bands.push_back (b);
        }
        else if (a == "--gain")     gainDb = std::atof (next());
        else if (a == "--invert")   invert = true;
        else if (a == "--bypass")   bypass = true;
        else if (a == "--block")    block = std::max (1, std::atoi (next()));
        else if (a == "--response") responsePath = next();
        else { std::fprintf (stderr, "option inconnue : %s\n", a.c_str()); usage(); return 1; }
    }

    Audio audio;
    const std::string input = argv[1];

    if (input.rfind ("gen:", 0) == 0)
    {
        if (! generate (input.substr (4), 48000.0, audio))
        {
            std::fprintf (stderr, "signal inconnu : %s (gen:sweep ou gen:pink)\n", input.c_str());
            return 1;
        }
    }
    else
    {
        std::string err;

        if (! readWav (input, audio, err)) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
    }

    EqEngine engine;
    engine.prepare (audio.sampleRate, block);

    for (size_t i = 0; i < bands.size(); ++i)
        engine.setBand ((int) i, bands[i]);

    engine.setOutput (gainDb, invert);
    engine.setBypass (bypass);

    const size_t frames = audio.ch[0].size();
    float peak = 0.0f;

    for (size_t pos = 0; pos < frames; pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, frames - pos);
        float* ptrs[2] = { audio.ch[0].data() + pos, audio.channels > 1 ? audio.ch[1].data() + pos : nullptr };
        engine.process (ptrs, audio.channels, n);
    }

    for (const auto& c : audio.ch)
        for (float v : c)
            peak = std::max (peak, std::abs (v));

    if (! writeWav (argv[2], audio)) { std::fprintf (stderr, "écriture impossible : %s\n", argv[2]); return 1; }

    std::printf ("%s -> %s : %zu échantillons, %.0f Hz, %d canal(aux), %zu bande(s), crête %.2f dBFS%s\n", argv[1], argv[2], frames,
                 audio.sampleRate, audio.channels, bands.size(), 20.0 * std::log10 (std::max (peak, 1.0e-9f)),
                 peak > 1.0f ? "  (dépasse 0 dBFS : fichier en float, baisse --gain pour un export en 16 bits)" : "");

    if (! responsePath.empty())
    {
        std::ofstream csv (responsePath);
        csv << "frequence_hz,numerique_db,analogique_db\n";

        for (int i = 0; i <= 400; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 400.0);
            double dig = 0.0, ana = 0.0;

            for (const auto& b : bands)
            {
                if (! b.used || ! b.on) continue;
                dig += magnitudeDb (designFilter (b.filter, audio.sampleRate), f, audio.sampleRate);
                ana += analogMagnitudeDb (b.filter, f);
            }
            csv << f << ',' << dig << ',' << ana << '\n';
        }
        std::printf ("courbe de réponse : %s\n", responsePath.c_str());
    }

    return 0;
}
