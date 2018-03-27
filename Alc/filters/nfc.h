#ifndef FILTER_NFC_H
#define FILTER_NFC_H

struct NfcFilter1 {
    float g;
    float coeffs[1*2 + 1];
    float history[1];
};
struct NfcFilter2 {
    float g;
    float coeffs[2*2 + 1];
    float history[2];
};
struct NfcFilter3 {
    float g;
    float coeffs[3*2 + 1];
    float history[3];
};

typedef struct NfcFilter {
    struct NfcFilter1 first;
    struct NfcFilter2 second;
    struct NfcFilter3 third;
} NfcFilter;


/* NOTE:
 * w0 = speed_of_sound / (source_distance * sample_rate);
 * w1 = speed_of_sound / (control_distance * sample_rate);
 *
 * Generally speaking, the control distance should be approximately the average
 * speaker distance, or based on the reference delay if outputing NFC-HOA. It
 * must not be negative, 0, or infinite. The source distance should not be too
 * small relative to the control distance.
 */

void NfcFilterCreate(NfcFilter *nfc, const float w0, const float w1);
void NfcFilterAdjust(NfcFilter *nfc, const float w0);

/* Near-field control filter for first-order ambisonic channels (1-3). */
void NfcFilterProcess1(NfcFilter *nfc, float *restrict dst, const float *restrict src, const int count);

/* Near-field control filter for second-order ambisonic channels (4-8). */
void NfcFilterProcess2(NfcFilter *nfc, float *restrict dst, const float *restrict src, const int count);

/* Near-field control filter for third-order ambisonic channels (9-15). */
void NfcFilterProcess3(NfcFilter *nfc, float *restrict dst, const float *restrict src, const int count);

#endif /* FILTER_NFC_H */
