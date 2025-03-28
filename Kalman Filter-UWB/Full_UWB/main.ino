#include <SimpleKalmanFilter.h>

// Declare Kalman filter objects for X, Y, and Z positions
SimpleKalmanFilter kf_x(1, 1, 0.1);
SimpleKalmanFilter kf_y(1, 1, 0.1);
SimpleKalmanFilter kf_z(1, 1, 0.1);

void setup() {
    Serial.begin(115200);

    // Set process noise and measurement error for the Kalman filter
    kf_x.setProcessNoise(0.1);   // Lower process noise
    kf_x.setMeasurementError(0.5);  // Slightly higher measurement error

    kf_y.setProcessNoise(0.1);
    kf_y.setMeasurementError(0.5);

    kf_z.setProcessNoise(0.1);
    kf_z.setMeasurementError(0.5);
}

void loop() {
    // Simulate changing distances (replace with actual UWB values)
    static float distance_x = 10.0;
    static float distance_y = 20.0;
    static float distance_z = 30.0;

    // Simulate movement with smaller random changes
    distance_x += random(-1, 1);  // Smaller random movement
    distance_y += random(-1, 1);
    distance_z += random(-1, 1);

    // Apply Kalman filter to smooth out distance measurements
    float filtered_x = kf_x.updateEstimate(distance_x);
    float filtered_y = kf_y.updateEstimate(distance_y);
    float filtered_z = kf_z.updateEstimate(distance_z);

    // Print results
    Serial.print("Filtered X: "); Serial.print(filtered_x);
    Serial.print(" | Filtered Y: "); Serial.print(filtered_y);
    Serial.print(" | Filtered Z: "); Serial.println(filtered_z);

    delay(1000);  // Add delay to simulate real-time measurements
}
