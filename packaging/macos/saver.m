/*
 * The screensaver as a macOS screen saver module (a .saver bundle), the kind that is
 * installed and then chosen in System Settings.
 *
 * macOS loads the module and asks it to draw into a view of its own, so there is no
 * window to open and no main loop to run. The screensaver's own source is compiled
 * in whole, below; each frame it draws with SDL's software renderer into a block of
 * memory, and that picture is handed to macOS.
 *
 * Compiled with -DSAVER_SOURCE='"src/....c"' and -DSAVER_CLASS=SomethingView. Every
 * screensaver must have a class name of its own, because they may be loaded into the
 * same process.
 */
/* The system's headers first, and SDL's and C's, so that nothing in the screensaver's
 * source can disturb them. Then the few names that the old Mac headers already use
 * (they are a style, a style and a 64-bit number there) are given to the
 * screensaver under other names. */
#import <ScreenSaver/ScreenSaver.h>
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define outline saver_outline
#define shadow saver_shadow
#define wide saver_wide
#define main app_main_not_used
#include SAVER_SOURCE
#undef main

#define MAX_HEIGHT 900 /* drawn no larger than this and scaled up, to keep the frame rate */

static int started;       /* the show is shared by every view: one per screen, and the preview */
static double last_step;

@interface SAVER_CLASS : ScreenSaverView {
    SDL_Surface *canvas;
    SDL_Renderer *painter;
    SDL_Texture *pictures[NSPRITE];
}
@end

@implementation SAVER_CLASS

- (instancetype)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview
{
    self = [super initWithFrame:frame isPreview:isPreview];
    if (!self) return nil;
    [self setAnimationTimeInterval:1.0 / 30.0];
    if (!started) { started = 1; load_sprites(); start_show(); }
    /* Since macOS 14 the system does not stop a screen saver's process when the screen
     * saver ends, and it goes on running unseen. It says when it is stopping; leave then. */
    if (!isPreview)
        [[NSDistributedNotificationCenter defaultCenter] addObserver:self selector:@selector(systemIsStopping:)
                                                                name:@"com.apple.screensaver.willstop" object:nil];
    return self;
}

- (void)systemIsStopping:(NSNotification *)note
{
    (void)note;
    if (@available(macOS 14.0, *)) exit(0);
}

- (void)dropCanvas
{
    if (painter) {
        for (int i = 0; i < NSPRITE; i++) if (pictures[i]) { SDL_DestroyTexture(pictures[i]); pictures[i] = NULL; }
        SDL_DestroyRenderer(painter); painter = NULL;
    }
    if (canvas) { SDL_FreeSurface(canvas); canvas = NULL; }
}

- (BOOL)canvasOfWidth:(int)w height:(int)h
{
    if (canvas && canvas->w == w && canvas->h == h) return YES;
    [self dropCanvas];
    canvas = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    painter = canvas ? SDL_CreateSoftwareRenderer(canvas) : NULL;
    if (!painter) { [self dropCanvas]; return NO; }
    SDL_SetRenderDrawBlendMode(painter, SDL_BLENDMODE_BLEND);
    make_textures(painter, pictures);
    return YES;
}

- (void)animateOneFrame
{
    double now = [NSDate timeIntervalSinceReferenceDate];
    if (last_step == 0) last_step = now;
    double dt = now - last_step;
    if (dt > 0.004) { /* once a frame, however many views there are */
        last_step = now;
        update(dt > 0.1 ? 0.1 : dt);
    }
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    NSRect bounds = [self bounds], px = [self convertRectToBacking:bounds];
    double shrink = px.size.height > MAX_HEIGHT ? MAX_HEIGHT / px.size.height : 1;
    int w = (int)(px.size.width * shrink), h = (int)(px.size.height * shrink);
    if (w < 16 || h < 16 || ![self canvasOfWidth:w height:h]) return;
    cur_tex = pictures;
    draw_scene(painter, w, h);
    SDL_RenderPresent(painter);

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmap = CGBitmapContextCreate(canvas->pixels, w, h, 8, canvas->pitch, space,
                                                kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst);
    CGImageRef image = bitmap ? CGBitmapContextCreateImage(bitmap) : NULL;
    CGContextRef screen = [[NSGraphicsContext currentContext] CGContext];
    if (image && screen) {
        CGContextSetInterpolationQuality(screen, kCGInterpolationMedium);
        CGContextDrawImage(screen, NSRectToCGRect(bounds), image);
    }
    if (image) CGImageRelease(image);
    if (bitmap) CGContextRelease(bitmap);
    CGColorSpaceRelease(space);
}

- (BOOL)isOpaque { return YES; }
- (BOOL)hasConfigureSheet { return NO; }
- (NSWindow *)configureSheet { return nil; }

- (void)dealloc
{
    [[NSDistributedNotificationCenter defaultCenter] removeObserver:self];
    [self dropCanvas];
}

@end
