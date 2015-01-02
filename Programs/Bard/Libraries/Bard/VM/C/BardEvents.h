#ifndef BARD_EVENTS_H
#define BARD_EVENTS_H

#define BARD_EVENT_UPDATE 0
// No args

#define BARD_EVENT_DRAW   1
// (window_id,display_width,display_height,clip_x,clip_y,clip_width,clip_height):Integer

#define BARD_EVENT_POINTER_FOCUS 2
// (window_id:Integer,gained:Integer)

#define BARD_EVENT_POINTER_ACTION 3 
// (action=[movement/0|press/1|release/2],window_id:Integer,x:Real,y:Real,button_index:Integer)
#define BARD_EVENT_POINTER_ACTION_MOVEMENT 0 
#define BARD_EVENT_POINTER_ACTION_PRESS    1 
#define BARD_EVENT_POINTER_ACTION_RELEASE  2 

#define BARD_EVENT_KEY_ACTION 4
// (action=[press/0,repeat/1,release/2],window_id,unicode,keycode,syscode):Integer[5]
#define BARD_EVENT_KEY_ACTION_PRESS   0
#define BARD_EVENT_KEY_ACTION_REPEAT  1
#define BARD_EVENT_KEY_ACTION_RELEASE 2

#define BARD_EVENT_REQUEST_QUIT 5

#endif

