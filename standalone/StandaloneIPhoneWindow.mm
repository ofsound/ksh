#import <UIKit/UIKit.h>

@interface KshStandaloneIPhoneController : UIViewController
@end

@implementation KshStandaloneIPhoneController

- (UIRectEdge) preferredScreenEdgesDeferringSystemGestures
{
    return UIRectEdgeAll;
}

- (UIInterfaceOrientationMask) supportedInterfaceOrientations
{
    return self.childViewControllers.firstObject.supportedInterfaceOrientations;
}

- (UIViewController*) childViewControllerForStatusBarHidden
{
    return self.childViewControllers.firstObject;
}

- (UIViewController*) childViewControllerForStatusBarStyle
{
    return self.childViewControllers.firstObject;
}

- (UIViewController*) childViewControllerForHomeIndicatorAutoHidden
{
    return self.childViewControllers.firstObject;
}

@end

void kshDeferIPhoneEdgeGestures (void* nativeView);

void kshDeferIPhoneEdgeGestures (void* nativeView)
{
    UIView* view = (__bridge UIView*) nativeView;
    UIWindow* window = view.window;
    if (window.traitCollection.userInterfaceIdiom != UIUserInterfaceIdiomPhone)
        return;

    UIViewController* content = window.rootViewController;
    if (content == nil || [content isKindOfClass: [KshStandaloneIPhoneController class]])
        return;

    // UIKit requires the old controller to leave the window before it can become a child.
    auto* wrapper = [[KshStandaloneIPhoneController alloc] init];
   #if ! __has_feature(objc_arc)
    [content retain];
   #endif
    window.rootViewController = nil;
    [wrapper addChildViewController: content];
    content.view.frame = wrapper.view.bounds;
    content.view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [wrapper.view addSubview: content.view];
    [content didMoveToParentViewController: wrapper];
    window.rootViewController = wrapper;
    [wrapper setNeedsUpdateOfScreenEdgesDeferringSystemGestures];

   #if ! __has_feature(objc_arc)
    [content release];
    [wrapper release];
   #endif
}
