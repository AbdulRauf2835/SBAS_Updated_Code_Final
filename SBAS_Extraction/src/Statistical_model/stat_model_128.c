#include "stat_model_128.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int add_sbas_data(StatPredictor128* self, const StatModel_MT9_Data* entry) {
    if (self->data_size >= self->data_capacity) {
        int new_cap = (self->data_capacity == 0) ? 16 : self->data_capacity * 2;
        StatModel_MT9_Data* new_data = (StatModel_MT9_Data*)realloc(self->data, sizeof(StatModel_MT9_Data) * new_cap);
        if (!new_data) return -1;
        self->data = new_data;
        self->data_capacity = new_cap;
    }
    self->data[self->data_size++] = *entry;
    return 0;
}

static int double_array_add(double** arr, int* size, int* capacity, double value) {
    if (*size >= *capacity) {
        int new_cap = (*capacity == 0) ? 8 : *capacity * 2;
        double* new_arr = (double*)realloc(*arr, sizeof(double) * new_cap);
        if (!new_arr) return -1;
        *arr = new_arr;
        *capacity = new_cap;
    }
    (*arr)[*size] = value;
    (*size)++;
    return 0;
}

static int add_movement(double** arr, int* size, int* cap, double v1, double v2, double v3) {
    if (double_array_add(arr, size, cap, v1) != 0) return -1;
    if (double_array_add(arr, size, cap, v2) != 0) return -1;
    if (double_array_add(arr, size, cap, v3) != 0) return -1;
    return 0;
}

void stat_model_128_init(StatPredictor128* self) {
    memset(self, 0, sizeof(StatPredictor128));
    self->initialized = false;
}

void stat_model_128_free(StatPredictor128* self) {
    free(self->data);
    free(self->movementPattern);
    free(self->rateMovementPattern);
    stat_model_128_init(self);
}

void stat_model_128_add_training_data(StatPredictor128* self, const StatModel_MT9_Data* mt9_data) {
    add_sbas_data(self, mt9_data);
}

void stat_model_128_analyze(StatPredictor128* self) {
    if (self->data_size < 2) {
        self->initialized = false;
        return;
    }
    printf("STAT_MODEL_128: Analyzing movement pattern...\n");

    free(self->movementPattern);
    free(self->rateMovementPattern);
    self->movementPattern = NULL;
    self->rateMovementPattern = NULL;
    self->movementPattern_size = 0;
    self->rateMovementPattern_size = 0;
    self->movementPattern_capacity = 0;
    self->rateMovementPattern_capacity = 0;

    for (int i = 1; i < self->data_size; i++) {
        double deltaX = self->data[i].xg_m - self->data[i-1].xg_m;
        double deltaY = self->data[i].yg_m - self->data[i-1].yg_m;
        double deltaZ = self->data[i].zg_m - self->data[i-1].zg_m;

        if (fabs(deltaX) > 1.0 || fabs(deltaY) > 1.0 || fabs(deltaZ) > 1.0) {
            add_movement(&self->movementPattern, &self->movementPattern_size, &self->movementPattern_capacity, deltaX, deltaY, deltaZ);
            add_movement(&self->rateMovementPattern, &self->rateMovementPattern_size, &self->rateMovementPattern_capacity,
                self->data[i].xg_rate, self->data[i].yg_rate, self->data[i].zg_rate);
            self->stationaryCount = 0;
        } else {
            self->stationaryCount++;
        }
    }

    self->stationaryCount = 0;
    for (int i = self->data_size - 1; i > 0 && i > self->data_size - 4; i--) {
        double deltaX = self->data[i].xg_m - self->data[i-1].xg_m;
        double deltaY = self->data[i].yg_m - self->data[i-1].yg_m;
        double deltaZ = self->data[i].zg_m - self->data[i-1].zg_m;

        if (fabs(deltaX) < 1.0 && fabs(deltaY) < 1.0 && fabs(deltaZ) < 1.0) {
            self->stationaryCount++;
        } else {
            break;
        }
    }
    
    if (self->data_size > 0) {
        self->live_data = self->data[self->data_size - 1];
        self->initialized = true;
    }
    printf("STAT_MODEL_128: Analysis complete. Stationary count: %d. Initialized: %d\n", self->stationaryCount, self->initialized);
}

const StatModel_MT9_Data* stat_model_128_predict(StatPredictor128* self) {
    int num_patterns = self->movementPattern_size / 3;
    StatModel_MT9_Data* current = &self->live_data;

    if (self->stationaryCount >= 2 && num_patterns > 0) {
        double avgX = 0, avgY = 0, avgZ = 0;
        double avgXRate = 0, avgYRate = 0, avgZRate = 0;
        
        int count = (num_patterns < 3) ? num_patterns : 3;

        for (int i = num_patterns - count; i < num_patterns; i++) {
            avgX += self->movementPattern[i*3 + 0];
            avgY += self->movementPattern[i*3 + 1];
            avgZ += self->movementPattern[i*3 + 2];

            avgXRate += self->rateMovementPattern[i*3 + 0];
            avgYRate += self->rateMovementPattern[i*3 + 1];
            avgZRate += self->rateMovementPattern[i*3 + 2];
        }

        avgX /= count;
        avgY /= count;
        avgZ /= count;
        avgXRate /= count;
        avgYRate /= count;
        avgZRate /= count;

        current->xg_m += avgX;
        current->yg_m += avgY;
        current->zg_m += avgZ;
        current->xg_rate = avgXRate;
        current->yg_rate = avgYRate;
        current->zg_rate = avgZRate;

        self->stationaryCount = 0;
    } else {
        self->stationaryCount++;
    }

    return &self->live_data;
}