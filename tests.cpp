﻿#include "dictlsd/IDictionaryDecoder.h"
#include "dictlsd/DictionaryReader.h"
#include "dictlsd/LenTable.h"
#include "dictlsd/BitStream.h"
#include "dictlsd/ArticleHeading.h"
#include "dictlsd/CachePage.h"
#include "ZipWriter.h"
#include "dictlsd/tools.h"

#include <gtest/gtest.h>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/interprocess/streams/bufferstream.hpp>
#include <tuple>
#include <algorithm>
#include <vector>
#include <fstream>
#include <iostream>
#include <fstream>

using namespace dictlsd;

static_assert(sizeof(unsigned) == 4, "");

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

void assertContains(unsigned* arr, std::vector<std::tuple<unsigned, unsigned>> content) {
    unsigned tmparr[64] = {0};
    for (auto t : content) {
        tmparr[std::get<0>(t)] = std::get<1>(t);
    }
    for (unsigned i = 0; i < 64; i++) {
        ASSERT_EQ(tmparr[i], arr[i]);
    }
}

#define TestBitStream()

std::string printCode(int val, int len) {
    std::string res;
    do {
        len--;
        int bit = (val >> len) & 1;
        res += boost::lexical_cast<std::string>(bit);
    } while (len > 0);
    return res;
}

TEST(Tests, bitStreamTest) {
    uint8_t buf[] = { 0x13, 0xF0, 0xF9, 0x11, 0x12, 0x45 };
    BitStreamAdapter bstr(new InMemoryStream(buf, sizeof(buf)));
    unsigned b1 = bstr.read(3);
    unsigned b2 = bstr.read(4);
    unsigned b3 = bstr.read(1);
    ASSERT_EQ(0x13, (b1 << 5) | (b2 << 1) | b3);
    ASSERT_EQ(1, bstr.tell());
    ASSERT_EQ(0xF0, bstr.read(8));
    ASSERT_EQ(2, bstr.tell());
    bstr.seek(0);
    ASSERT_EQ(0, bstr.tell());
    bstr.read(4);
    bstr.seek(1);
    ASSERT_EQ(0xF, bstr.read(4));
    bstr.seek(0);
    ASSERT_EQ(0x13F0F911, bstr.read(32));
    bstr.seek(2);
    ASSERT_EQ(0xF9111245, bstr.read(32));
    bstr.seek(0);
    char b[2];
    bstr.readSome(b, 2);
    ASSERT_EQ(2, bstr.tell());
}

TEST(Tests, decoderTest) {
    std::fstream f("simple_testdict1/test.lsd", std::ios::in | std::ios::binary);
    ASSERT_TRUE(f.is_open());
    char buf[1333];
    f.read(buf, 1333);
    BitStreamAdapter bstr(new InMemoryStream(buf, 1333));
    LSDDictionary decoder(&bstr);

    std::u16string goodName = u"Country Capital Dictionary [en-en]";
    ASSERT_TRUE(goodName == decoder.name());
    LSDHeader header = decoder.header();
    ASSERT_EQ(0x142001, header.version);
    ASSERT_EQ(0x9341A792, header.checksum);
    ASSERT_EQ(3, header.entriesCount);
    ASSERT_EQ(1033, header.sourceLanguage);
    ASSERT_EQ(1033, header.targetLanguage);
    std::u16string goodAnno = u"yjsakfabcdaskdhabbdfjkgh1jkh33jkhj331ddj\n";
    ASSERT_TRUE(goodAnno == decoder.annotation());

    auto heads = decoder.readHeadings();
    ASSERT_TRUE(u"abcd" == decoder.readArticle(heads[0].articleReference()));
    ASSERT_TRUE(u"aabb33" == decoder.readArticle(heads[1].articleReference()));
    ASSERT_TRUE(u"1234" == decoder.readArticle(heads[2].articleReference()));
}

void assertFilesAreEqual(std::string path1, std::string path2) {
    auto flags = std::ios::in | std::ios::binary;
    std::ifstream file1(path1, flags);
    std::ifstream file2(path2, flags);
    ASSERT_TRUE(file1.is_open());
    ASSERT_TRUE(file2.is_open());
    for (;;) {
        char ch1, ch2;
        file1.read(&ch1, 1);
        file2.read(&ch2, 1);
        if (!file1.eof() && !file2.eof()) {
            ASSERT_EQ(ch1, ch2);
            continue;
        }
        break;
    }
    ASSERT_TRUE(file2.eof());
}

TEST(Tests, userLsdHeadingsTest) {
    std::fstream f("simple_testdict1/headingsTestDict1.lsd", std::ios::in | std::ios::binary);
    ASSERT_TRUE(f.is_open());
    char buf[1447];
    f.read(buf, 1447);
    BitStreamAdapter bstr(new InMemoryStream(buf, 1447));
    LSDDictionary reader(&bstr);
    LSDHeader header = reader.header();
    bstr.seek(header.pagesOffset);

    CachePage page;
    page.loadHeader(bstr);
    ASSERT_EQ(true, page.isLeaf());
    ASSERT_EQ(7, page.headingsCount());

    auto heads = reader.readHeadings();
    ASSERT_EQ(u"Abc", heads[0].dslText());
    ASSERT_EQ(u"Abcde", heads[1].dslText());
    ASSERT_EQ(u"Abcdefg", heads[2].dslText());
    ASSERT_EQ(u"Abcdefg123", heads[3].dslText());
    ASSERT_EQ(u"Abcdefg123ZzzzZ", heads[4].dslText());
    ASSERT_EQ(u"anotherone", heads[5].dslText());
    ASSERT_EQ(u"Zzxx", heads[6].dslText());
}

TEST(Tests, extHeadingsTest) {
    std::fstream f("simple_testdict1/testext.lsd", std::ios::in | std::ios::binary);
    ASSERT_TRUE(f.is_open());
    char buf[1390];
    f.read(buf, 1390);
    BitStreamAdapter bstr(new InMemoryStream(buf, 1390));
    LSDDictionary reader(&bstr);

    ASSERT_EQ(2, reader.header().entriesCount);

    auto heads = reader.readHeadings();
    ASSERT_EQ(u"Abc {[sub]}e{[/sub]}", heads[0].dslText());
    ASSERT_EQ(u"bipolar {(}affective{)} disorder", heads[1].dslText());
}

TEST(Tests, unsortedHeadingsTest) {
    std::fstream f("simple_testdict1/unsorted_testdict.lsd", std::ios::in | std::ios::binary);
    ASSERT_TRUE(f.is_open());
    char buf[1280];
    f.read(buf, 1280);
    BitStreamAdapter bstr(new InMemoryStream(buf, 1280));
    LSDDictionary reader(&bstr);

    ASSERT_EQ(10, reader.header().entriesCount);

    auto heads = reader.readHeadings();
    ASSERT_EQ(uR"!(\[\\{ab}\])!", heads[0].dslText());
    ASSERT_EQ(uR"!(\[{ab}\])!", heads[1].dslText());
    ASSERT_EQ(uR"!(\[a\~b{cd}ef\])!", heads[2].dslText());
    ASSERT_EQ(uR"!(\[ab\{{cd}ef\])!", heads[3].dslText());
    ASSERT_EQ(uR"!(\[ab{cd}ef\])!", heads[4].dslText());
    ASSERT_EQ(uR"!(\\1ab{cd}\])!", heads[5].dslText());
    ASSERT_EQ(uR"!(\\2ab{(cd)}\])!", heads[6].dslText());
    ASSERT_EQ(uR"!(\\3ab{abcd})!", heads[7].dslText());
    ASSERT_EQ(uR"!(ab{cd}ef)!", heads[8].dslText());
    ASSERT_EQ(uR"!(bb{c\~d}e)!", heads[9].dslText());
}

TEST(Tests, collapseVariantHeadingsTest) {
    std::fstream f("simple_testdict1/variants_testdict.lsd", std::ios::in | std::ios::binary);
    ASSERT_TRUE(f.is_open());
    char buf[1291];
    f.read(buf, 1291);
    BitStreamAdapter bstr(new InMemoryStream(buf, 1291));
    LSDDictionary reader(&bstr);

    ASSERT_EQ(12, reader.header().entriesCount);
    auto heads = reader.readHeadings();
    ASSERT_EQ(12, heads.size());
    collapseVariants(heads);
    ASSERT_EQ(5, heads.size());

    ASSERT_EQ(u"(1)z", heads[0].dslText());
    ASSERT_EQ(u"bbb(12)34", heads[1].dslText());
    ASSERT_EQ(u"ccc(12)dd(34)", heads[2].dslText());
    ASSERT_EQ(u"ddd(12)e{e(34)}", heads[3].dslText());
    ASSERT_EQ(u"e(ab{12}cd)ef", heads[4].dslText());
}

TEST(Tests, collapseVariantHeadingsTest2) {
    std::fstream f("simple_testdict1/variants_testdict2.lsd", std::ios::in | std::ios::binary);
    ASSERT_TRUE(f.is_open());
    char buf[1306];
    f.read(buf, 1306);
    BitStreamAdapter bstr(new InMemoryStream(buf, 1306));
    LSDDictionary reader(&bstr);

    ASSERT_EQ(6, reader.header().entriesCount);
    auto heads = reader.readHeadings();
    ASSERT_EQ(6, heads.size());
    collapseVariants(heads);
    ASSERT_EQ(4, heads.size());

    ASSERT_EQ(u"abc (123)", heads[0].dslText());
    ASSERT_EQ(u"alternative", heads[1].dslText());
    ASSERT_EQ(u"headings", heads[2].dslText());
    ASSERT_EQ(u"bbb (123) z", heads[3].dslText());
}
