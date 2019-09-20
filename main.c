#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include "fbink.h"

#define ERRCODE(e) (-(e))

#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _b : _a; })

// Globals:
int fbfd = -1;
FBInkConfig fbink_cfg = { 0 };

typedef struct myrect
{
    unsigned int x;
    unsigned int y;
    unsigned int width;
    unsigned int height;
} myrect_t;

int rect1InsideRect2(myrect_t rect1, myrect_t rect2)
{
    if( (rect1.x >= rect2.x && rect1.x <= rect2.x+rect2.width) || (rect1.x+rect1.width >= rect2.x && rect1.x+rect1.width <= rect2.x+rect2.width) ){
        if(rect1.y >= rect2.y && rect1.y <= rect2.y+rect2.height)
            return true;
        if(rect1.y+rect1.height >= rect2.y && rect1.y+rect1.height <= rect2.y+rect2.height)
            return true;
    }
}

int rectsIntersect(myrect_t rect1, myrect_t rect2)
{
    return rect1InsideRect2(rect1, rect2) || rect1InsideRect2(rect2,rect1);
}

myrect_t rectsMerge(myrect_t rect1, myrect_t rect2)
{
    /*printf("\nMerging:\n");
    printf("rect1: %i %i %i %i\n", rect1.x, rect1.y, rect1.width, rect1.height);
    printf("rect1: %i %i %i %i\n", rect2.x, rect2.y, rect2.width, rect2.height);*/
    myrect_t r;
    r.x = min(rect1.x, rect2.x);
    r.y = min(rect1.y, rect2.y);
    r.width = max(rect1.x+rect1.width, rect2.x+rect2.width)-r.x;
    r.height = max(rect1.y+rect1.height, rect2.y+rect2.height)-r.y;
    //printf("r    : %i %i %i %i\n", r.x, r.y, r.width, r.height);
    return r;
}

typedef struct areaListItem
{
    myrect_t area;
    unsigned int areaState;
    struct timespec lastUpdate;
    struct timespec timeOfCreation;
    struct areaListItem* next;
} areaListItem_t;
areaListItem_t* areaList = NULL;

typedef enum
{
    RM_PRETTY,
    RM_QUICKLY,
    RM_PRETTY_CLEANUP
} REFRESH_MODE_T;

void refresh(unsigned int refresh_mode, myrect_t area)
{
    
    
    switch(refresh_mode) {
    case RM_PRETTY:
        fbink_cfg.wfm_mode = WFM_AUTO; //GC16_FAST;
        fbink_cfg.is_flashing = false;
        break;
    case RM_QUICKLY:
        fbink_cfg.wfm_mode = WFM_A2;
        fbink_cfg.is_flashing = false;
        break;
    case RM_PRETTY_CLEANUP:
        fbink_cfg.wfm_mode = WFM_AUTO; //GC16_FAST;
        fbink_cfg.is_flashing = true;
        break;
        
    }
    printf("DOING fbink_refresh: %i %i %i %i\n", area.y, area.x, area.width, area.height);
    fbink_refresh(fbfd, area.y, area.x, area.width, area.height, HWD_ORDERED, &fbink_cfg);
    
}

typedef enum
{
    AS_DISPOSE,
    AS_DRAWN_PRETTY,
    AS_DRAWN_QUICKLY
} AREA_STATE_T;

unsigned int msElapsedSince(struct timespec since)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    unsigned int sec_elapsed = now.tv_sec-since.tv_sec;
    int msec_elapsed = ((long)now.tv_nsec-(long)since.tv_nsec)/1e6;
    //printf("Elapsed: %is %dms\n", sec_elapsed, msec_elapsed);
    return sec_elapsed*1000+msec_elapsed;
    
    
}

void handleDamagedArea(myrect_t area)
{
    areaListItem_t* currentItem = areaList;
    areaListItem_t* previousItem = NULL;
    
    while(currentItem) { // Search for corresponding area
        if( rectsIntersect(currentItem->area, area) ) { // Related area found
                currentItem->area = rectsMerge(currentItem->area, area);
                unsigned int tdiffms = msElapsedSince(currentItem->lastUpdate);
                printf("RELATED area found! tdiffms:%i\n", tdiffms);
                if(currentItem->areaState == AS_DRAWN_QUICKLY || (tdiffms<500) && msElapsedSince(currentItem->timeOfCreation)>200) { // Fast updates, switch to quick refresh
                    printf("Doing QUICK update\n");
                    refresh(RM_QUICKLY, currentItem->area);
                    currentItem->areaState = AS_DRAWN_QUICKLY;
                    clock_gettime(CLOCK_REALTIME, &(currentItem->lastUpdate));
                    return;
                }
                else { // Slow updates, pretty refresh
                    if(currentItem->areaState == AS_DRAWN_QUICKLY) { // cleanup A2 ghosting
                        printf("Doing PRETTY_CLEANUP update\n");
                        refresh(RM_PRETTY_CLEANUP, currentItem->area);
                    }
                    else {
                        printf("Doing PRETTY update\n");
                        refresh(RM_PRETTY, currentItem->area);
                    }
                    currentItem->areaState = AS_DRAWN_PRETTY;
                    clock_gettime(CLOCK_REALTIME, &(currentItem->lastUpdate));
                    return;
                    
                }
            }
            else { // Area is not related
                // Do nothing and continue the search   
                previousItem = currentItem;
                currentItem = currentItem->next;
            }
    }
    
    // If execution reaches this, no corresponding area was found. -> new entry
    printf("NEW entry\n");
    currentItem = malloc(sizeof(areaListItem_t));
    currentItem->next = NULL;
    currentItem->area = area;
    refresh(RM_PRETTY, area);
    currentItem->areaState = AS_DRAWN_PRETTY;
    clock_gettime(CLOCK_REALTIME, &(currentItem->lastUpdate));
    currentItem->timeOfCreation = currentItem->lastUpdate;
    if(previousItem)
        previousItem->next = currentItem;
    else
        areaList = currentItem;
    
}

void areaListHousekeeping(void)
{
    int areactr = 1;
    areaListItem_t* currentItem = areaList;
    areaListItem_t* previousItem = NULL;
    while(currentItem) {
        unsigned int tdiffms = msElapsedSince(currentItem->lastUpdate);///((float)CLOCKS_PER_SEC)*1000;
        //printf("Housekeeping: area age (ms): %f\n", tdiffms);
        if( tdiffms>500) { // Delete tracked area
            printf("DELETE area %i, tdiffms %i\n", areactr, tdiffms);
            if(currentItem->areaState == AS_DRAWN_QUICKLY) { // cleanup A2 ghosting
                printf("Doing PRETTY_CLEANUP update (Housekeeping)\n");
                refresh(RM_PRETTY_CLEANUP, currentItem->area);
            }
            areaListItem_t* rememberToFree = currentItem;
            if(previousItem) {
                previousItem->next = currentItem->next;
            }
            else {
                areaList = NULL;
            }
            free(rememberToFree);
        }
        previousItem = currentItem;
        currentItem = currentItem->next;
        areactr++;
    }
}


int main(int argc, char *argv[])
{
    Display* display = XOpenDisplay(NULL);
    Window root = DefaultRootWindow(display);

    //XDAMAGE INIT
    int damage_event, damage_error, test;
    test = XDamageQueryExtension(display, &damage_event, &damage_error);
    Damage damage = XDamageCreate(display, root, XDamageReportNonEmpty);
    
    XEvent event;
    XDamageNotifyEvent *devent;
     
    // Assume success, until shit happens ;)
	int rv = EXIT_SUCCESS;

	// Init FBInk
	int fbfd = -1;
	// Open framebuffer and keep it around, then setup globals.
	if ((fbfd = fbink_open()) == ERRCODE(EXIT_FAILURE))
    {
		fprintf(stderr, "Failed to open the framebuffer, aborting . . .\n");
		rv = ERRCODE(EXIT_FAILURE);
		goto cleanup;
	}
	if (fbink_init(fbfd, &fbink_cfg) == ERRCODE(EXIT_FAILURE))
    {
		fprintf(stderr, "Failed to initialize FBInk, aborting . . .\n");
		rv = ERRCODE(EXIT_FAILURE);
		goto cleanup;
	}
    fbink_refresh(fbfd, 0,0,0,0, HWD_ORDERED, &fbink_cfg);                 
    while (1)
    {
        if( XPending(display) ) {
            XNextEvent(display,&event);
            
            if( fbink_cfg.is_verbose )
                printf("Got event! type:%i\n", event.type);

            devent = (XDamageNotifyEvent*)&event;
            XserverRegion region = XFixesCreateRegion(display, NULL, 0);
            XDamageSubtract(display, devent->damage, None, region);
            int count;
            XRectangle* area = XFixesFetchRegion(display, region, &count);
            if(area){
                for(int i=0; i < count; i++){
                    XRectangle rect = area[i];
                    if( fbink_cfg.is_verbose )
                        printf("Damaged area: x:%hi y:%hi width:%hi height:%hi\n", rect.x, rect.y, rect.width, rect.height);
                    myrect_t area;
                    area.x = rect.x;
                    area.y = rect.y;
                    area.width = rect.width;
                    area.height = rect.height;
                    handleDamagedArea(area);
                    //fbink_refresh(fbfd, rect.y, rect.x, rect.width, rect.height, HWD_ORDERED, &fbink_cfg);                 
                }
                XFree(area);
            }
            XFixesDestroyRegion(display, region);
        }
        else {
            areaListHousekeeping();
            usleep(50000);
            //printf("Clock: %li\n", clock());
        }
    }
    XCloseDisplay(display);
        
    
    // Cleanup
cleanup:
	if (fbink_close(fbfd) == ERRCODE(EXIT_FAILURE))
    {
		fprintf(stderr, "Failed to close the framebuffer, aborting . . .\n");
		rv = ERRCODE(EXIT_FAILURE);
	}

	return rv;

}
