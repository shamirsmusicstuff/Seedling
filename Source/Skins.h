#pragma once
// Skins: a skin is just FIVE colours - the same thing a coolors.co palette is.
//
//   Documents/Seedling/Skins/<anything>.seedskin
//
//   {
//     "name": "My Skin",
//     "colors": ["202030", "39304A", "635C51", "7D7461", "B0A990"]
//   }
//
// The roles are worked out automatically (darkest = background, lightest = text/highlights,
// the most vivid / best-contrasting of the middle three = neon accent, the other two = secondary tones).
// To pick the roles yourself, add   "roles": { "ink": "..", "accent": "..", "slate": "..", "mist": "..", "paper": ".." }
// A plain text file also works: put "name: My Skin" on the first line and anything with five hex colours
// after it, e.g. a pasted coolors.co URL:   https://coolors.co/0c120c-c20114-6d7275-c7d6d5-ecebf3

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace seed
{

struct Skin
{
    juce::String name;
    juce::Colour ink, accent, slate, mist, paper;
    // ink = background, accent = neon / active colour, slate = lines and tracks,
    // mist = secondary text and curves, paper = main text and highlights
};

namespace skins
{
    struct Builtin { const char* name; const char* colors[5]; };

    inline const Builtin kBuiltin[] =
    {
        { "Seedling", { "0C120C", "C20114", "6D7275", "C7D6D5", "ECEBF3" } },
        { "Skin 1",   { "202030", "39304A", "635C51", "7D7461", "B0A990" } },
        { "Skin 2",   { "14110F", "34312D", "7E7F83", "D9C5B2", "F3F3F4" } },
        { "Skin 3",   { "3B1F2B", "DB162F", "DBDFAC", "5F758E", "383961" } },
    };

    inline float lum (juce::Colour c)
    {
        auto f = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * f (c.getFloatRed()) + 0.7152f * f (c.getFloatGreen()) + 0.0722f * f (c.getFloatBlue());
    }

    inline float contrast (juce::Colour a, juce::Colour b)
    {
        float la = lum (a), lb = lum (b);
        if (la < lb) std::swap (la, lb);
        return (la + 0.05f) / (lb + 0.05f);
    }

    // Five colours in any order -> roles.
    inline Skin assignRoles (const juce::String& name, std::array<juce::Colour, 5> c)
    {
        std::sort (c.begin(), c.end(), [] (juce::Colour a, juce::Colour b) { return lum (a) < lum (b); });

        Skin s;
        s.name = name;
        s.ink = c[0];
        s.paper = c[4];

        int best = 1;
        float bestScore = -1.f;
        for (int i = 1; i <= 3; ++i)
        {
            const float score = c[(size_t) i].getSaturation() + std::min (1.f, (contrast (c[(size_t) i], c[0]) - 1.f) / 5.f);
            if (score > bestScore) { bestScore = score; best = i; }
        }
        s.accent = c[(size_t) best];

        std::vector<juce::Colour> rest;
        for (int i = 1; i <= 3; ++i) if (i != best) rest.push_back (c[(size_t) i]);   // already dark -> light
        s.slate = rest[0];
        s.mist = rest[1];
        return s;
    }

    inline bool hexColour (juce::String tok, juce::Colour& out)
    {
        tok = tok.trim().trimCharactersAtStart ("#");
        if (tok.length() != 6 || ! tok.containsOnly ("0123456789abcdefABCDEF")) return false;
        out = juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) tok.getHexValue32()));
        return true;
    }

    // Parses the text of a skin file. Returns false (with a reason in `error`) if it is not a valid skin.
    inline bool parse (const juce::String& text, const juce::String& fallbackName, Skin& out, juce::String& error)
    {
        const auto t = text.trim();
        juce::String name = fallbackName;
        std::vector<juce::Colour> cols;
        juce::var roles;

        if (t.startsWithChar ('{'))
        {
            const auto v = juce::JSON::parse (t);
            if (! v.isObject()) { error = "not valid JSON"; return false; }
            if (v["name"].toString().isNotEmpty()) name = v["name"].toString();
            if (auto* arr = v["colors"].getArray())
                for (auto& e : *arr) { juce::Colour c; if (hexColour (e.toString(), c)) cols.push_back (c); }
            roles = v["roles"];
        }
        else
        {
            for (auto line : juce::StringArray::fromLines (t))
            {
                const auto l = line.trim();
                if (l.startsWithIgnoreCase ("name:")) { name = l.fromFirstOccurrenceOf (":", false, false).trim(); continue; }
                if (l.startsWith ("//")) continue;
                for (auto& tok : juce::StringArray::fromTokens (l, " ,;-/:.\t\"[]()", ""))
                {
                    juce::Colour c;
                    if (hexColour (tok, c)) cols.push_back (c);
                }
            }
        }

        name = name.trim();
        if (name.isEmpty()) name = "Unnamed";

        if (roles.isObject())
        {
            juce::Colour ink, accent, slate, mist, paper;
            if (hexColour (roles["ink"].toString(), ink) && hexColour (roles["accent"].toString(), accent)
                && hexColour (roles["slate"].toString(), slate) && hexColour (roles["mist"].toString(), mist)
                && hexColour (roles["paper"].toString(), paper))
            {
                out = { name, ink, accent, slate, mist, paper };
                return true;
            }
            error = "\"roles\" needs ink, accent, slate, mist and paper as 6-digit hex colours";
            return false;
        }

        if (cols.size() < 5)
        {
            error = "needs 5 hex colours (found " + juce::String ((int) cols.size()) + ")";
            return false;
        }
        out = assignRoles (name, { cols[0], cols[1], cols[2], cols[3], cols[4] });
        return true;
    }

    inline juce::String toJson (const juce::String& name, const char* const* hex)
    {
        return "{\n  \"name\": \"" + name + "\",\n  \"colors\": [\"" + hex[0] + "\", \"" + hex[1] + "\", \"" + hex[2]
               + "\", \"" + hex[3] + "\", \"" + hex[4] + "\"]\n}\n";
    }

    //==========================================================================
    inline juce::File rootDir() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Seedling"); }
    inline juce::File dir()     { return rootDir().getChildFile ("Skins"); }

    inline juce::String readmeText()
    {
        return "SEEDLING SKINS\n"
               "==============\n\n"
               "A skin is a small text file in this folder with the extension .seedskin.\n"
               "It is just five colours - the same as a coolors.co palette.\n\n"
               "Copy any skin, edit the name and the five hex colours, save, then pick \"Reload skins\" in the plugin's skin menu.\n\n"
               "  {\n"
               "    \"name\": \"My Skin\",\n"
               "    \"colors\": [\"202030\", \"39304A\", \"635C51\", \"7D7461\", \"B0A990\"]\n"
               "  }\n\n"
               "Colours can be in any order. Seedling works out the roles:\n"
               "  darkest            -> background\n"
               "  lightest           -> text and highlights\n"
               "  most vivid of the middle three (that still stands out from the background) -> neon accent\n"
               "  the other two      -> lines/tracks (darker) and secondary text/curves (lighter)\n"
               "Tip: make sure the darkest colour is dark and the lightest is light, or the text will be hard to read.\n\n"
               "To choose the roles yourself, add:\n"
               "    \"roles\": { \"ink\": \"..\", \"accent\": \"..\", \"slate\": \"..\", \"mist\": \"..\", \"paper\": \"..\" }\n\n"
               "A plain text file works too: a first line \"name: My Skin\" and then five hex colours anywhere,\n"
               "for example a pasted coolors.co link:\n"
               "    name: My Skin\n"
               "    https://coolors.co/0c120c-c20114-6d7275-c7d6d5-ecebf3\n";
    }

    inline void ensureFactory()
    {
        const auto d = dir();
        d.createDirectory();
        if (d.findChildFiles (juce::File::findFiles, false, "*.seedskin").isEmpty())
            for (auto& b : kBuiltin)
                d.getChildFile (juce::String (b.name) + ".seedskin").replaceWithText (toJson (b.name, b.colors));
        const auto readme = d.getChildFile ("README.txt");
        if (! readme.existsAsFile()) readme.replaceWithText (readmeText());
    }

    inline std::vector<Skin> builtins()
    {
        std::vector<Skin> out;
        for (auto& b : kBuiltin)
        {
            Skin s; juce::String err;
            if (parse (toJson (b.name, b.colors), b.name, s, err)) out.push_back (s);
        }
        return out;
    }

    // Reads every *.seedskin in the skins folder (creating the folder and the starter skins on first run).
    inline std::vector<Skin> loadAll (juce::StringArray* problems = nullptr)
    {
        ensureFactory();
        auto found = dir().findChildFiles (juce::File::findFiles, false, "*.seedskin");
        std::vector<juce::File> files (found.begin(), found.end());
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareNatural (b.getFileName()) < 0; });

        std::vector<Skin> out;
        for (auto& f : files)
        {
            Skin s; juce::String err;
            if (parse (f.loadFileAsString(), f.getFileNameWithoutExtension(), s, err)) out.push_back (s);
            else if (problems != nullptr) problems->add (f.getFileName() + ": " + err);
        }
        if (out.empty()) out = builtins();
        return out;
    }

    // The skin you picked last is remembered for every new plugin window.
    inline juce::String loadCurrentName()
    {
        const auto f = rootDir().getChildFile ("current_skin.txt");
        return f.existsAsFile() ? f.loadFileAsString().trim() : juce::String();
    }
    inline void saveCurrentName (const juce::String& n)
    {
        rootDir().createDirectory();
        rootDir().getChildFile ("current_skin.txt").replaceWithText (n);
    }
}

} // namespace seed
