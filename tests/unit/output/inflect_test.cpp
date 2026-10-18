#include "hdTrans/output/inflect.h"

#include "hdtest.h"

using namespace hdTrans::output::internal;

HD_TEST(inflect_classes_and_flags) {
    using Class = InflectionClass;
    using Pos = KoreanPartOfSpeech;
    struct Case {
        const char* stem;
        Class cls;
        uint16_t flags;
        Pos pos;
        char marker;
        char code;
        const char* expected;
    };
    const Case cases[] = {
        {"먹", Class::Regular, 0, Pos::Verb, '&', '0', "먹는"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '2', "먹으"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '3', "먹을"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '4', "먹는"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '5', "먹었"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '6', "먹어"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '7', "먹습"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '8', "먹은"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', '9', "먹읍"},
        {"먹", Class::Regular, 0, Pos::Verb, '&', 'B', "먹음"},
        {"가", Class::Regular, 0, Pos::Verb, '&', '4', "간"},
        {"가", Class::Regular, 0, Pos::Verb, '&', '5', "갔"},
        {"가", Class::Regular, 0, Pos::Verb, '&', '6', "가"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', '0', "사는"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', '2', "살"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', '3', "살"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', '7', "삽"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', '8', "산"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', 'A', "사"},
        {"살", Class::Rieul, 0, Pos::Verb, '&', 'B', "삼"},
        {"살", Class::Rieul, kNominalRieulMieum, Pos::Verb, '&', 'B', "삶"},
        {"긋", Class::Siot, 0, Pos::Verb, '&', '2', "그으"},
        {"긋", Class::Siot, 0, Pos::Verb, '&', '6', "그어"},
        {"노랗", Class::Hieuh, 0, Pos::Adjective, '$', '4', "노란"},
        {"노랗", Class::Hieuh, 0, Pos::Adjective, '$', '5', "노랬"},
        {"파랗", Class::Hieuh, 0, Pos::Adjective, '$', '6', "파래"},
        {"하얗", Class::Hieuh, 0, Pos::Adjective, '$', '6', "하얘"},
        {"푸", Class::U, 0, Pos::Verb, '&', '6', "퍼"},
        {"쓰", Class::Eu, 0, Pos::Verb, '&', '6', "써"},
        {"아프", Class::Eu, 0, Pos::Adjective, '$', '6', "아파"},
        {"프", Class::Eu, kBrightEu, Pos::Adjective, '$', '6', "파"},
        {"듣", Class::Digeut, 0, Pos::Verb, '&', '2', "들으"},
        {"듣", Class::Digeut, 0, Pos::Verb, '&', '6', "들어"},
        {"줍", Class::Bieup, 0, Pos::Verb, '&', '2', "주우"},
        {"줍", Class::Bieup, 0, Pos::Verb, '&', '3', "주울"},
        {"줍", Class::Bieup, 0, Pos::Verb, '&', '6', "주워"},
        {"돕", Class::Bieup, kBieupWa, Pos::Verb, '&', '6', "도와"},
        {"하", Class::Yeo, 0, Pos::Verb, '&', '5', "했"},
        {"하", Class::Yeo, 0, Pos::Verb, '&', '6', "해"},
        {"푸르", Class::Reo, 0, Pos::Adjective, '$', '6', "푸르러"},
        {"모르", Class::Reu, 0, Pos::Verb, '&', '6', "몰라"},
        {"부르", Class::Reu, 0, Pos::Verb, '&', '6', "불러"},
        {"있", Class::Existential, 0, Pos::Adjective, '$', '4', "있는"},
        {"없", Class::Existential, 0, Pos::Adjective, '%', '4', "없는"},
        {"이", Class::Copula, 0, Pos::Adjective, '&', '0', "인"},
        {"이", Class::Copula, 0, Pos::Adjective, '&', '5', "(이었)였"},
        {"이", Class::Copula, 0, Pos::Adjective, '&', '6', "(이어)여"},
        {"보", Class::Regular, 0, Pos::Verb, '&', '6', "봐"},
        {"보", Class::Regular, kNoContraction, Pos::Verb, '&', '6', "보아"},
        {"주", Class::Regular, 0, Pos::Verb, '&', '6', "줘"},
        {"주", Class::Regular, kNoContraction, Pos::Verb, '&', '6', "주어"},
        {"지", Class::Regular, 0, Pos::Verb, '&', '6', "져"},
        {"이", Class::Regular, 0, Pos::Verb, '&', '6', "이어"},
        {"이", Class::Regular, kContractIeung, Pos::Verb, '&', '6', "여"},
        {"깨", Class::Regular, 0, Pos::Verb, '&', '6', "깨어"},
        {"깨", Class::Regular, kDropAe, Pos::Verb, '&', '6', "깨"},
        {"세", Class::Regular, 0, Pos::Verb, '&', '6', "세어"},
        {"세", Class::Regular, kDropE, Pos::Verb, '&', '6', "세"},
        {"되", Class::Regular, 0, Pos::Verb, '&', '6', "되어"},
        {"뛰", Class::Regular, 0, Pos::Verb, '&', '6', "뛰어"},
        {"먹", Class::Regular, kFormalHortative, Pos::Verb, '&', '9', "먹습"},
        {"멀", Class::Rieul, kPastHortative, Pos::Adjective, '&', '9', "멀었습"},
        {"있", Class::Regular, kBareDeclarative, Pos::Verb, '&', '4', "있"},
        {"멀", Class::Rieul, 0, Pos::Adjective, '&', '0', "먼"},
        {"멀", Class::Rieul, 0, Pos::Adjective, '&', '4', "멀"},
        {"않", Class::Regular, 0, Pos::Verb, '$', '4', "않는"},
        {"살", Class::Rieul, 0, Pos::Verb, '%', '4', "사는"},
        {"높", Class::Regular, 0, Pos::Adjective, '$', '4', "높은"},
        {"높", Class::Regular, 0, Pos::Adjective, '$', '8', "높음"},
        {"가", Class::Regular, 0, Pos::Verb, '&', 'C', ""},
        {"가", Class::Regular, 0, Pos::Verb, '$', '0', ""},
        {"가", Class::Regular, 0, Pos::Verb, '%', '9', ""},
        {"가", Class::Regular, 0, Pos::Verb, '?', '1', ""},
        {"", Class::Regular, 0, Pos::Verb, '&', '1', ""},
        {"A", Class::Regular, 0, Pos::Verb, '&', '1', ""},
        {"가\xed", Class::Regular, 0, Pos::Verb, '&', '1', ""},
    };
    for (const auto& c : cases) {
        HD_CHECK_EQ(InflectStem(c.stem, c.pos, c.cls, c.flags, c.marker, c.code), std::string(c.expected));
    }
}

HD_TEST(inflect_membership_and_exceptions) {
    for (const auto& c : {std::pair{"&LT@", '5'}, {"&CJ@", 'C'}, {"$DH@", '9'}, {"$J6H", '8'}}) {
        const auto* family = FindKoreanFamily(c.first);
        HD_REQUIRE(family != nullptr);
        HD_CHECK(!Inflect(*family, c.second).empty());
    }
    const auto* missingNominal = FindKoreanFamily("&LA@");
    HD_REQUIRE(missingNominal != nullptr);
    HD_CHECK(Inflect(*missingNominal, 'B').empty());
    HD_CHECK(!InflectStem(missingNominal->stem, missingNominal->partOfSpeech, missingNominal->inflectionClass,
                         missingNominal->flags, '&', 'B').empty());
    HD_CHECK(FindKoreanFamily("&zz@") == nullptr);
    HD_CHECK(FindKoreanFamily("&A1@1") == nullptr);
    HD_CHECK_EQ(InflectionFormBit('C'), uint16_t{4096});
    HD_CHECK_EQ(InflectionFormBit('D'), uint16_t{0});
    HD_CHECK_EQ(InflectionFormBit('\0'), uint16_t{0});
}
