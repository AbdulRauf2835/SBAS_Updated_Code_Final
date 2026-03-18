#ifndef STAT_MODEL_128_H
#define STAT_MODEL_128_H

#include "stat_model_types.h"
#include <stdbool.h>

typedef struct {
    StatModel_MT9_Data* data;
    int data_size;
    int data_capacity;
    double* movementPattern;
    int movementPattern_size;
    int movementPattern_capacity;
    double* rateMovementPattern;
    int rateMovementPattern_size;
    int rateMovementPattern_capacity;
    int stationaryCount;
    bool initialized;
    StatModel_MT9_Data live_data;
} StatPredictor128;

void stat_model_128_init(StatPredictor128* self);
void stat_model_128_free(StatPredictor128* self);
void stat_model_128_add_training_data(StatPredictor128* self, const StatModel_MT9_Data* mt9_data);
void stat_model_128_analyze(StatPredictor128* self);
const StatModel_MT9_Data* stat_model_128_predict(StatPredictor128* self);

#endif // STAT_MODEL_128_H