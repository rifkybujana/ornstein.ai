#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test framework */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %s... ", #name); \
    name(); \
    tests_passed++; \
    printf("OK\n"); \
} while (0)

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    abort(); \
} } while (0)

#include "../src/face.h"
#include "../src/sentiment.h"

/* ── sentiment_parse tests ─────────────────────────────── */

static void test_sentiment_basic(void) {
    Emotion e = EMOTION_NEUTRAL;
    int end = 0;
    int ok = sentiment_parse("[MOOD:happy] hello", &e, &end);
    CHECK(ok == 1);
    CHECK(e == EMOTION_HAPPY);
    CHECK(end == 13);
}

static void test_sentiment_case_insensitive(void) {
    Emotion e = EMOTION_NEUTRAL;
    int end = 0;
    int ok = sentiment_parse("[MOOD:Happy] test", &e, &end);
    CHECK(ok == 1);
    CHECK(e == EMOTION_HAPPY);
    (void)end;
}

static void test_sentiment_all_emotions(void) {
    const struct { const char *tag; Emotion expected; } cases[] = {
        {"[MOOD:neutral]",   EMOTION_NEUTRAL},
        {"[MOOD:happy]",     EMOTION_HAPPY},
        {"[MOOD:excited]",   EMOTION_EXCITED},
        {"[MOOD:surprised]", EMOTION_SURPRISED},
        {"[MOOD:sleepy]",    EMOTION_SLEEPY},
        {"[MOOD:bored]",     EMOTION_BORED},
        {"[MOOD:curious]",   EMOTION_CURIOUS},
        {"[MOOD:sad]",       EMOTION_SAD},
        {"[MOOD:thinking]",  EMOTION_THINKING},
    };
    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        Emotion e = EMOTION_NEUTRAL;
        int end = 0;
        char buf[64];
        snprintf(buf, sizeof(buf), "%s text", cases[i].tag);
        int ok = sentiment_parse(buf, &e, &end);
        CHECK(ok == 1);
        CHECK(e == cases[i].expected);
        (void)end;
    }
}

static void test_sentiment_no_tag(void) {
    Emotion e = EMOTION_NEUTRAL;
    int end = 0;
    CHECK(sentiment_parse("hello world", &e, &end) == 0);
    CHECK(sentiment_parse("", &e, &end) == 0);
    CHECK(sentiment_parse(NULL, &e, &end) == 0);
    (void)e; (void)end;
}

static void test_sentiment_leading_whitespace(void) {
    Emotion e = EMOTION_NEUTRAL;
    int end = 0;
    int ok = sentiment_parse("  [MOOD:sad] text", &e, &end);
    CHECK(ok == 1);
    CHECK(e == EMOTION_SAD);
    (void)end;
}

static void test_sentiment_invalid_emotion(void) {
    Emotion e = EMOTION_NEUTRAL;
    int end = 0;
    int ok = sentiment_parse("[MOOD:angry] text", &e, &end);
    CHECK(ok == 0);
    (void)e; (void)end;
}

static void test_sentiment_unclosed_tag(void) {
    Emotion e = EMOTION_NEUTRAL;
    int end = 0;
    int ok = sentiment_parse("[MOOD:happy text", &e, &end);
    CHECK(ok == 0);
    (void)e; (void)end;
}

/* ── face_params_lerp tests ────────────────────────────── */

static void test_lerp_identity(void) {
    FaceParams a = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    FaceParams b = a;
    face_params_lerp(&a, &b, 5.0f, 0.016f);
    CHECK(a.eye_openness > 0.99f && a.eye_openness < 1.01f);
    CHECK(a.mouth_curve > -0.01f && a.mouth_curve < 0.01f);
}

static void test_lerp_convergence(void) {
    FaceParams a = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    FaceParams b = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    for (int i = 0; i < 1000; i++)
        face_params_lerp(&a, &b, 5.0f, 0.016f);
    CHECK(a.eye_openness > 0.99f);
    CHECK(a.mouth_curve > 0.99f);
    CHECK(a.pupil_offset_x > 0.99f);
}

static void test_lerp_zero_dt(void) {
    FaceParams a = {0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f};
    FaceParams b = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    face_params_lerp(&a, &b, 5.0f, 0.0f);
    CHECK(a.eye_openness > 0.49f && a.eye_openness < 0.51f);
}

/* ── face_emotion_preset tests ────────────────────────── */

static void test_emotion_preset_valid(void) {
    for (int e = 0; e < EMOTION_COUNT; e++) {
        const FaceParams *p = face_emotion_preset((Emotion)e);
        CHECK(p != NULL);
    }
}

static void test_emotion_preset_invalid(void) {
    const FaceParams *p = face_emotion_preset((Emotion)-1);
    CHECK(p != NULL);
    const FaceParams *neutral = face_emotion_preset(EMOTION_NEUTRAL);
    CHECK(p == neutral);
}

/* ── Main ──────────────────────────────────────────────── */

int main(void) {
    printf("=== ornstein unit tests ===\n\n");

    printf("sentiment_parse:\n");
    TEST(test_sentiment_basic);
    TEST(test_sentiment_case_insensitive);
    TEST(test_sentiment_all_emotions);
    TEST(test_sentiment_no_tag);
    TEST(test_sentiment_leading_whitespace);
    TEST(test_sentiment_invalid_emotion);
    TEST(test_sentiment_unclosed_tag);

    printf("\nface_params_lerp:\n");
    TEST(test_lerp_identity);
    TEST(test_lerp_convergence);
    TEST(test_lerp_zero_dt);

    printf("\nface_emotion_preset:\n");
    TEST(test_emotion_preset_valid);
    TEST(test_emotion_preset_invalid);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
