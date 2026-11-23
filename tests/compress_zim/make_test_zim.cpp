// make_test_zim.cpp — builds the small ZIM archive tests/test_compress_zim.aiml reads
// (spec/33_2 §6, F33-2-34: "Tests build a small ZIM with the libzim writer").
//
// Written in C++ rather than aiml because AIML reads ZIM files and does not write them (§6),
// so the writer side has no aiml surface to use.  Contents:
//   Kupfer        text/html   an article
//   Zinn          text/html   a second article, to have more than one cluster order position
//   Notiz         text/plain  a non-article item
//   Copper        -> Kupfer   a redirect that resolves
//   Ring_A        -> Ring_B   a redirect pair pointing at each other: the loop of F33-2-23
//   Ring_B        -> Ring_A
// plus the metadata Title/Language/Date that §6.1 names.
//
// Usage: make_test_zim <output.zim>

#include <zim/writer/creator.h>
#include <zim/writer/item.h>
#include <zim/uuid.h>

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: make_test_zim <output.zim>\n");
        return 2;
    }
    const std::string out = argv[1];
    try {
        zim::writer::Creator creator;
        creator.configVerbose(false);
        creator.configIndexing(false, "deu");     // no xapian index: the test does not search
        creator.setMainPath("Kupfer");
        creator.startZimCreation(out);

        const zim::writer::Hints article{{zim::writer::FRONT_ARTICLE, 1}};
        const zim::writer::Hints plain{};

        creator.addItem(zim::writer::StringItem::create(
            "Kupfer", "text/html", "Kupfer", article,
            "<html><body><h1>Kupfer</h1><p>Ein chemisches Element.</p></body></html>"));
        creator.addItem(zim::writer::StringItem::create(
            "Zinn", "text/html", "Zinn", article,
            "<html><body><h1>Zinn</h1><p>Ein weiteres Element.</p></body></html>"));
        creator.addItem(zim::writer::StringItem::create(
            "Notiz", "text/plain", "Notiz", plain,
            "just a note, not an article"));

        creator.addRedirection("Copper", "Copper", "Kupfer", article);
        // The pair below points at each other — resolve must report a loop, not spin.
        creator.addRedirection("Ring_A", "Ring A", "Ring_B", plain);
        creator.addRedirection("Ring_B", "Ring B", "Ring_A", plain);

        creator.addMetadata("Title", "AIML ZIM Testarchiv");
        creator.addMetadata("Language", "deu");
        creator.addMetadata("Date", "2026-11-03");

        creator.finishZimCreation();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "make_test_zim: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "make_test_zim: unknown error\n");
        return 1;
    }
    std::printf("wrote %s\n", out.c_str());
    return 0;
}
