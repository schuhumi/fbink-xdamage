#include <stdio.h>
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
    
    FBInkConfig fbink_cfg = { 0U };
    
	// optional flash/verbose to make stuff more obvious
	fbink_cfg.is_flashing = false;
    fbink_cfg.is_verbose  = true;
    
    fbink_cfg.wfm_mode = WFM_GC16_FAST;
    
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
    
    while (1)
    {
        
        XNextEvent(display,&event);
        
        if( fbink_cfg.is_verbose )
            printf("Got event! type:%i\n", event.type);

        if( 1 )//event.type == XDamageNotify)
        {
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
                    fbink_refresh(fbfd, rect.y, rect.x, rect.width, rect.height, HWD_ORDERED, &fbink_cfg);                 
                }
                XFree(area);
            }
            XFixesDestroyRegion(display, region);
        }

        
        //XDamageSubtract(display,damage,None,None);
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
