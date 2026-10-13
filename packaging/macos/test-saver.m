/* Load a screen saver module the way macOS does, let it run a few frames, and save
 * what it drew:  test-saver Some.saver out.png */
#import <Cocoa/Cocoa.h>
#import <ScreenSaver/ScreenSaver.h>

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc < 3) return 2;
        [NSApplication sharedApplication];
        NSBundle *bundle = [NSBundle bundleWithPath:[NSString stringWithUTF8String:argv[1]]];
        if (![bundle load]) { fprintf(stderr, "the module did not load\n"); return 1; }
        Class cls = [bundle principalClass];
        if (!cls || ![cls isSubclassOfClass:[ScreenSaverView class]]) { fprintf(stderr, "no screen saver class in it\n"); return 1; }
        ScreenSaverView *view = [[cls alloc] initWithFrame:NSMakeRect(0, 0, 1280, 720) isPreview:YES];
        if (!view) { fprintf(stderr, "the view could not be made\n"); return 1; }
        [view startAnimation];
        for (int i = 0; i < 90; i++) { [view animateOneFrame]; [NSThread sleepForTimeInterval:1.0 / 30]; }
        NSBitmapImageRep *rep = [view bitmapImageRepForCachingDisplayInRect:[view bounds]];
        [view cacheDisplayInRect:[view bounds] toBitmapImageRep:rep];
        [view stopAnimation];
        NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        if (![png writeToFile:[NSString stringWithUTF8String:argv[2]] atomically:YES]) return 1;
        printf("%s: %s drew %ld x %ld\n", argv[1], [NSStringFromClass(cls) UTF8String], (long)[rep pixelsWide], (long)[rep pixelsHigh]);
    }
    return 0;
}
