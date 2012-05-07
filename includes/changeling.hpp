
// Changeling profanity delay
// James Harrison <james@talkunafraid.co.uk>

/// Our various possible running states
typedef enum {
  /// Pass audio without modification
  CHANGELING_STATE_OUT=0,
  /// Playing buffer at realtime, recording to end of buffer
  CHANGELING_STATE_IN,
  /// Playing jingle and recording
  CHANGELING_STATE_ENTERING,
  /// Playing buffer at realtime, not recording
  CHANGELING_STATE_LEAVING,
  /// Dumping buffer
  CHANGELING_STATE_DUMPING,
  /// Shutting down
  CHANGELING_STATE_EXITING,
  /// Starting up
  CHANGELING_STATE_STARTING
} ChangelingRunState;