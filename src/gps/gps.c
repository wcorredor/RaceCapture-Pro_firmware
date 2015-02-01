#include "auto_track.h"
#include "dateTime.h"
#include "gps.h"
#include "geopoint.h"
#include "geoCircle.h"
#include "launch_control.h"
#include "loggerHardware.h"
#include "LED.h"
#include "loggerConfig.h"
#include "modp_numtoa.h"
#include "modp_atonum.h"
#include "mod_string.h"
#include "predictive_timer_2.h"
#include "printk.h"
#include "tracks.h"

#include <stdint.h>

#define GPS_LOCK_FLASH_COUNT 5
#define GPS_NOFIX_FLASH_COUNT 50
#define LATITUDE_DATA_LEN 12
#define LONGITUDE_DATA_LEN 13
#define UTC_TIME_BUFFER_LEN 11
#define UTC_SPEED_BUFFER_LEN 10

// In Millis now.
#define START_FINISH_TIME_THRESHOLD 10000

#define TIME_NULL -1

static const Track * g_activeTrack;

static int g_configured;
static int g_flashCount;
static float g_prevLatitude;
static float g_prevLongitude;

static int g_atStartFinish;
static int g_prevAtStartFinish;
static tiny_millis_t g_lastStartFinishTimestamp;

static int g_atTarget;
static int g_prevAtTarget;
static tiny_millis_t g_lastSectorTimestamp;

static int g_sector;
static int g_lastSector;

static tiny_millis_t g_lastLapTime;
static tiny_millis_t g_lastSectorTime;

static int g_lapCount;
static float g_distance;


static float degreesToMeters(float degrees) {
   // There are 110574.27 meters per degree of latitude at the equator.
   return degrees * 110574.27;
}

static bool isGpsSignalUsable(enum GpsSignalQuality q) {
   return q != GPS_QUALITY_NO_FIX;
}

/**
 * @return true if we haven't parsed any data yet, false otherwise.
 */
static bool isGpsDataCold() {
   return g_utcMillisAtSample == 0;
}

GeoPoint getGeoPoint() {
   GeoPoint gp;

   gp.latitude = getLatitude();
   gp.longitude = getLongitude();

   return gp;
}

static void updateMillisSinceEpoch(DateTime fixDateTime) {
   g_utcMillisAtSample = getMillisecondsSinceUnixEpoch(fixDateTime);
}

millis_t getMillisSinceEpoch() {
   // If we have no GPS data, return 0 to indicate that.
   if (isGpsDataCold()){
      return 0;
   }

   const tiny_millis_t deltaSinceSample = getUptime() - g_uptimeAtSample;
   return g_utcMillisAtSample + deltaSinceSample;
}

long long getMillisSinceEpochAsLongLong() {
   return (long long) getMillisSinceEpoch();
}

static void updateUptimeAtSample() {
   g_uptimeAtSample = getUptime();
}

tiny_millis_t getUptimeAtSample() {
   return g_uptimeAtSample;
}

void resetGpsDistance() {
   g_distance = 0;
}

void setGpsDistanceKms(float dist) {
	g_distance = dist;
}

float getGpsDistanceKms() {
   return g_distance;
}

float getGpsDistanceMiles() {
	return KMS_TO_MILES_CONSTANT * g_distance;
}

void resetLapCount() {
   g_lapCount = 0;
}

int getLapCount() {
   return g_lapCount;
}

int getSector() {
   return g_sector;
}

int getLastSector() {
   return g_lastSector;
}

tiny_millis_t getLastLapTime() {
   return g_lastLapTime;
}

float getLastLapTimeInMinutes() {
   return tinyMillisToMinutes(getLastLapTime());
}

tiny_millis_t getLastSectorTime() {
   return g_lastSectorTime;
}

float getLastSectorTimeInMinutes() {
   return tinyMillisToMinutes(getLastSectorTime());
}

int getAtStartFinish() {
   return g_atStartFinish;
}

int getAtSector() {
   return g_atTarget;
}

float getLatitude() {
   return g_latitude;
}

float getLongitude() {
   return g_longitude;
}

enum GpsSignalQuality getGPSQuality() {
   return g_gpsQuality;
}

void setGPSQuality(enum GpsSignalQuality quality) {
   g_gpsQuality = quality;
}

int getSatellitesUsedForPosition() {
   return g_satellitesUsedForPosition;
}

float getGPSSpeed() {
   return g_speed;
}

float getGpsSpeedInMph() {
   return getGPSSpeed() * 0.621371192; //convert to MPH
}

void setGPSSpeed(float speed) {
   g_speed = speed;
}

DateTime getLastFixDateTime() {
   return g_dtLastFix;
}

/**
 * @return True if we have crossed the start line at least once, false otherwise.
 */
static bool isStartCrossedYet() {
   return g_lastStartFinishTimestamp != 0;
}

static float calcDistancesSinceLastSample(GpsSamp *gpsSample) {
   const GeoPoint prev = {g_prevLatitude, g_prevLongitude};

   if (!isValidPoint(&prev) || !isValidPoint(&gpsSample->point)){
      return 0.0;
   }
   // Return distance in KM
   return distPythag(&prev, &gpsSample->point) / 1000;
}

static int processStartFinish(const GpsSamp *gpsSample, const Track *track, const float targetRadius) {

   // First time crossing start finish.  Handle this in a special way.
   if (!isStartCrossedYet()) {
      lc_supplyGpsSample(gpsSample);

      if (lc_hasLaunched()) {
         g_lastStartFinishTimestamp = lc_getLaunchTime();
         g_lastSectorTimestamp = lc_getLaunchTime();
         g_prevAtStartFinish = 1;
         g_sector = 0;
         return true;
      }

      return false;
   }

   const tiny_millis_t timestamp = getMillisSinceFirstFix();
   const tiny_millis_t elapsed = timestamp - g_lastStartFinishTimestamp;
   const struct GeoCircle sfCircle = gc_createGeoCircle(getFinishPoint(track),
                                                        targetRadius);

   /*
    * Guard against false triggering. We have to be out of the start/finish
    * target for some amount of time and couldn't have been in there during our
    * last time in this method.
    *
    * FIXME: Should have logic that checks that we left the start/finish circle
    * for some time.
    */
   g_atStartFinish = gc_isPointInGeoCircle(&(gpsSample->point), sfCircle);

   if (!g_atStartFinish || g_prevAtStartFinish != 0 ||
       elapsed <= START_FINISH_TIME_THRESHOLD) {
      g_prevAtStartFinish = 0;
      return false;
   }

   // If here, we are at Start/Finish and have de-bounced duplicate entries.
   pr_debug_int(g_lapCount);
   pr_debug(" Lap Detected\r\n");
   g_lapCount++;
   g_lastLapTime = elapsed;
   g_lastStartFinishTimestamp = timestamp;
   g_prevAtStartFinish = 1;

   return true;
}

static void processSector(const Track *track, float targetRadius) {
   // We don't process sectors until we cross Start
   if (!isStartCrossedYet())
      return;

   const GeoPoint point = getSectorGeoPointAtIndex(track, g_sector);
   const struct GeoCircle sbCircle = gc_createGeoCircle(point, targetRadius);

   g_atTarget = gc_isPointInGeoCircle(getGeoPoint(), sbCircle);
   if (!g_atTarget) {
      g_prevAtTarget = 0;
      return;
   }

   /*
    * Past here we are sure we are at a sector boundary.
    */
   const tiny_millis_t millis = getMillisSinceFirstFix();
   pr_debug_int(g_sector);
   pr_debug(" Sector Boundary Detected\r\n");

   g_prevAtTarget = 1;
   g_lastSectorTime = millis - g_lastSectorTimestamp;
   g_lastSectorTimestamp = millis;
   g_lastSector = g_sector;
   ++g_sector;

   // Check if we need to wrap the sectors.
   GeoPoint next = getSectorGeoPointAtIndex(track, g_sector);
   if (areGeoPointsEqual(point, next))
      g_sector = 0;
}

void gpsConfigChanged(void) {
   g_configured = 0;
}

void initGPS() {
   g_configured = 0;
   g_activeTrack = NULL;
   g_utcMillisAtSample = 0;
   g_flashCount = 0;
   g_prevLatitude = 0.0;
   g_prevLongitude = 0.0;
   g_lastLapTime = 0;
   g_lastSectorTime = 0;
   g_atStartFinish = 0;
   g_prevAtStartFinish = 0;
   g_lastStartFinishTimestamp = 0;
   g_atTarget = 0;
   g_prevAtTarget = 0;
   g_lastSectorTimestamp = 0;
   g_lapCount = 0;
   g_distance = 0;
   g_sector = -1;
   g_lastSector = -1; // Indicates no previous sector.
   resetPredictiveTimer();
   g_dtFirstFix = g_dtLastFix = (DateTime) { 0 };
   g_uptimeAtSample = 0;
}

static void flashGpsStatusLed(enum GpsSignalQuality gpsQuality) {
   if (g_flashCount == 0)
      LED_disable(1);
   g_flashCount++;

   int targetFlashCount = isGpsSignalUsable(gpsQuality) ?
      GPS_LOCK_FLASH_COUNT : GPS_NOFIX_FLASH_COUNT;

   if (g_flashCount >= targetFlashCount) {
      LED_enable(1);
      g_flashCount = 0;
   }
}

tiny_millis_t getMillisSinceFirstFix() {
   return getTimeDeltaInTinyMillis(g_dtLastFix, g_dtFirstFix);
}

static int isStartFinishEnabled(const Track *track) {
    return isFinishPointValid(track) && isStartPointValid(track);
}

static int isSectorTrackingEnabled(const Track *track) {
    LoggerConfig *config = getWorkingLoggerConfig();

    // We must have at least one valid sector, which must start at position 0.  Else errors.
    GeoPoint p0 = getSectorGeoPointAtIndex(track, 0);
    return config->LapConfigs.sectorTimeCfg.sampleRate != SAMPLE_DISABLED &&
            isValidPoint(&p0) && isStartFinishEnabled(track);
}

static void onLocationUpdated(GpsSamp *gpsSample) {
   static int sectorEnabled = 0;
   static int startFinishEnabled = 0;

   // If no GPS lock, no point in doing any of this.
   if (!isGpsSignalUsable(gpsSample->quality))
      return;

   LoggerConfig *config = getWorkingLoggerConfig();
   const GeoPoint *gp = gpsSample->point;

   // FIXME: Improve on this.  Doesn't need calculation every time.
   const float targetRadius = degreesToMeters(config->TrackConfigs.radius);

   if (!g_configured) {
      TrackConfig *trackConfig = &(config->TrackConfigs);
      Track *defaultTrack = &trackConfig->track;
      g_activeTrack = trackConfig->auto_detect ? auto_configure_track(defaultTrack, gp) : defaultTrack;
      startFinishEnabled = isStartFinishEnabled(g_activeTrack);
      sectorEnabled = isSectorTrackingEnabled(g_activeTrack);
      lc_setup(g_activeTrack, targetRadius);
      g_configured = 1;
   }

   float dist = calcDistancesSinceLastSample(gpsSample);
   g_distance += dist;

   if (startFinishEnabled) {
      // Seconds since first fix is good until we alter the code to use millis directly
      const tiny_millis_t millisSinceFirstFix = getMillisSinceFirstFix();
      const int lapDetected = processStartFinish(g_activeTrack, targetRadius);

      if (lapDetected) {
         resetGpsDistance();

         /*
          * FIXME: Special handling of fisrt start/finish crossing.  Needed
          * b/c launch control will delay the first launch notification
          */
         if (getLapCount() == 0) {
            const GeoPoint sp = getStartPoint(g_activeTrack);
            // Distance is in KM
            g_distance = distPythag(&sp, gp) / 1000;

            startFinishCrossed(sp, g_lastStartFinishTimestamp);
            addGpsSample(gp, millisSinceFirstFix);
         } else {
            startFinishCrossed(gp, millisSinceFirstFix);
         }
      } else {
         addGpsSample(gp, millisSinceFirstFix);
      }

      if (sectorEnabled)
         processSector(g_activeTrack, targetRadius);
   }

   g_prevLatitude = gpsSample->point.latitude;
   g_prevLongitude = gpsSample->point.longitude;

}

int checksumValid(const char *gpsData, size_t len) {
   int valid = 0;
   unsigned char checksum = 0;
   size_t i = 0;
   for (; i < len - 1; i++) {
      char c = *(gpsData + i);
      if (c == '*' || c == '\0')
         break;
      else if (c == '$')
         continue;
      else
         checksum ^= c;
   }
   if (len > i + 2) {
      unsigned char dataChecksum = modp_xtoc(gpsData + i + 1);
      if (checksum == dataChecksum)
         valid = 1;
   }
   return valid;
}

void processGPSUpdate(GpsSamp *gpsSample){
	if (!isGpsDataCold()){
		  onLocationUpdated(gpsSample);
		  flashGpsStatusLed(gpsSample->quality);
	}
}

