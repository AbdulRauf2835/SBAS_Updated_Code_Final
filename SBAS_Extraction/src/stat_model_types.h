#ifndef STAT_MODEL_TYPES_H
#define STAT_MODEL_TYPES_H

#include "message_types.h" // For the full SBAS_MT9_Message

/* * This is the lightweight C struct, based on your C++ 'SBASData'.
 * We use a new name to avoid conflicts with your message_types.h.
 */
typedef struct {
    double xg_m;
    double yg_m;
    double zg_m;
    double xg_rate;
    double yg_rate;
    double zg_rate;
    double xg_accel;
    double yg_accel;
    double zg_accel;
} StatModel_MT9_Data;


/* Helper: convert from the full MT9 message to the lightweight model data */
static void copy_msg_to_model_data(const SBAS_MT9_Message *msg, StatModel_MT9_Data *d) {
    if (!msg || !d) return;
    d->xg_m = msg->xg_ecef;
    d->yg_m = msg->yg_ecef;
    d->zg_m = msg->zg_ecef;
    d->xg_rate = msg->xg_rate;
    d->yg_rate = msg->yg_rate;
    d->zg_rate = msg->zg_rate;
    d->xg_accel = msg->xg_accel;
    d->yg_accel = msg->yg_accel;
    d->zg_accel = msg->zg_accel;
}

/* Helper: convert from the model's data back to the full MT9 message */
static void copy_model_data_to_msg(const StatModel_MT9_Data *d, SBAS_MT9_Message *msg) {
    if (!msg || !d) return;
    // We only overwrite the 9 parameters.
    // t0, URA, agf0, and agf1 are left untouched.
    msg->xg_ecef = d->xg_m;
    msg->yg_ecef = d->yg_m;
    msg->zg_ecef = d->zg_m;
    msg->xg_rate = d->xg_rate;
    msg->yg_rate = d->yg_rate;
    msg->zg_rate = d->zg_rate;
    msg->xg_accel = d->xg_accel;
    msg->yg_accel = d->yg_accel;
    msg->zg_accel = d->zg_accel;
}

#endif // STAT_MODEL_TYPES_H