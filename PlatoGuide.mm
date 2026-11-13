// PlatoGuide.mm - patches the pool script in memory at load and adds an in-game "PG" menu.
// PG_MODE: 0 = loads, does nothing | 1 = hooks only | 2 = full patch + menu
#import <UIKit/UIKit.h>
#import <JavaScriptCore/JavaScriptCore.h>
#import <objc/runtime.h>
#include "pg_patch.inc"   // const char* kPatcherSrc (generated from pg_patch.js)

#ifndef PG_MODE
#define PG_MODE 2
#endif

static IMP g_eval1, g_eval2;
static JSContext* g_patcher;
static __weak JSContext* g_gameCtx;
static NSMutableDictionary* g_cfg;   // read by the patched script as global __pgCfg

static void PushCfg(void) {
    JSContext* c = g_gameCtx;
    if (c && g_cfg) c[@"__pgCfg"] = [g_cfg copy];
}

static NSString* PatchIfNeeded(JSContext* ctx, NSString* script) {
#if PG_MODE < 2
    return script;
#else
    if (![script isKindOfClass:NSString.class] || script.length < 200000) return script;
    if ([script rangeOfString:@"renderAimLines("].location == NSNotFound) return script;
    if ([script rangeOfString:@"__pgDraw"].location != NSNotFound) return script;
    @synchronized (NSObject.class) {
        if (!g_patcher) {
            g_patcher = [[JSContext alloc] init];
            ((JSValue*(*)(id, SEL, NSString*))g_eval1)(g_patcher, @selector(evaluateScript:),
                [NSString stringWithUTF8String:kPatcherSrc]);
        }
        JSValue* res = [g_patcher[@"pgPatch"] callWithArguments:@[script]];
        if ([res[@"ok"] toBool]) {
            g_gameCtx = ctx;
            PushCfg();
            return [res[@"src"] toString];
        }
    }
    return script;
#endif
}

static JSValue* hook1(JSContext* self, SEL _cmd, NSString* script) {
    return ((JSValue*(*)(id, SEL, NSString*))g_eval1)(self, _cmd, PatchIfNeeded(self, script));
}
static JSValue* hook2(JSContext* self, SEL _cmd, NSString* script, NSURL* url) {
    return ((JSValue*(*)(id, SEL, NSString*, NSURL*))g_eval2)(self, _cmd, PatchIfNeeded(self, script), url);
}

#if PG_MODE >= 2
// ---------------- menu ----------------
@interface PGWin : UIWindow @end
@implementation PGWin
- (UIView*)hitTest:(CGPoint)p withEvent:(UIEvent*)e {
    UIView* v = [super hitTest:p withEvent:e];
    return (v == self || v == self.rootViewController.view) ? nil : v;   // touches go to the game
}
@end

static NSArray* PGKeys(void)  { return @[@"guide", @"obj", @"cue"]; }
static NSArray* PGNames(void) { return @[@"Guideline", @"Object ball path", @"Cue ball path"]; }

@interface PGVC : UIViewController
@property (nonatomic, strong) UIButton* btn;
@property (nonatomic, strong) UIView* panel;
@property (nonatomic, strong) UILabel* status;
@property (nonatomic, strong) UILabel* bounceLabel;
@end

@implementation PGVC
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.clearColor;

    self.btn = [UIButton buttonWithType:UIButtonTypeSystem];
    self.btn.frame = CGRectMake(12, 90, 46, 46);
    self.btn.backgroundColor = [UIColor colorWithWhite:0 alpha:0.65];
    self.btn.layer.cornerRadius = 23;
    [self.btn setTitle:@"PG" forState:UIControlStateNormal];
    [self.btn setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    [self.btn addTarget:self action:@selector(togglePanel) forControlEvents:UIControlEventTouchUpInside];
    [self.btn addGestureRecognizer:[[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(drag:)]];
    [self.view addSubview:self.btn];

    self.panel = [[UIView alloc] initWithFrame:CGRectMake(12, 144, 250, 190)];
    self.panel.backgroundColor = [UIColor colorWithWhite:0 alpha:0.78];
    self.panel.layer.cornerRadius = 12;
    self.panel.hidden = YES;
    [self.view addSubview:self.panel];

    NSArray* names = PGNames();
    for (NSUInteger i = 0; i < names.count; i++) {
        UILabel* l = [[UILabel alloc] initWithFrame:CGRectMake(12, 12 + i * 38, 165, 30)];
        l.text = names[i]; l.textColor = UIColor.whiteColor; l.font = [UIFont systemFontOfSize:14];
        [self.panel addSubview:l];
        UISwitch* sw = [[UISwitch alloc] initWithFrame:CGRectMake(184, 12 + i * 38, 51, 31)];
        sw.on = [g_cfg[PGKeys()[i]] boolValue];
        sw.tag = (NSInteger)i;
        [sw addTarget:self action:@selector(flip:) forControlEvents:UIControlEventValueChanged];
        [self.panel addSubview:sw];
    }
    self.bounceLabel = [[UILabel alloc] initWithFrame:CGRectMake(12, 128, 120, 30)];
    self.bounceLabel.textColor = UIColor.whiteColor;
    self.bounceLabel.font = [UIFont systemFontOfSize:14];
    [self.panel addSubview:self.bounceLabel];
    UIStepper* st = [[UIStepper alloc] initWithFrame:CGRectMake(140, 128, 94, 32)];
    st.minimumValue = 0; st.maximumValue = 6; st.value = [g_cfg[@"bounces"] doubleValue];
    [st addTarget:self action:@selector(step:) forControlEvents:UIControlEventValueChanged];
    [self.panel addSubview:st];
    [self refreshBounceLabel];

    self.status = [[UILabel alloc] initWithFrame:CGRectMake(12, 162, 226, 20)];
    self.status.textColor = [UIColor colorWithWhite:0.75 alpha:1];
    self.status.font = [UIFont systemFontOfSize:11];
    [self.panel addSubview:self.status];
}
- (void)refreshBounceLabel {
    self.bounceLabel.text = [NSString stringWithFormat:@"Bounces: %d", [g_cfg[@"bounces"] intValue]];
}
- (void)togglePanel {
    self.panel.hidden = !self.panel.hidden;
    self.status.text = g_gameCtx ? @"game script patched" : @"waiting for game script...";
}
- (void)flip:(UISwitch*)sw {
    g_cfg[PGKeys()[sw.tag]] = @(sw.on ? 1 : 0);
    PushCfg();
}
- (void)step:(UIStepper*)st {
    g_cfg[@"bounces"] = @((int)st.value);
    PushCfg();
    [self refreshBounceLabel];
}
- (void)drag:(UIPanGestureRecognizer*)g {
    CGPoint d = [g translationInView:self.view];
    [g setTranslation:CGPointZero inView:self.view];
    self.btn.center = CGPointMake(self.btn.center.x + d.x, self.btn.center.y + d.y);
    CGRect f = self.panel.frame;
    f.origin = CGPointMake(self.btn.frame.origin.x, CGRectGetMaxY(self.btn.frame) + 8);
    self.panel.frame = f;
}
@end

static PGWin* g_win;

static void ShowMenu(void) {
    UIWindowScene* scene = nil;
    for (UIScene* sc in UIApplication.sharedApplication.connectedScenes)
        if ([sc isKindOfClass:UIWindowScene.class] && sc.activationState == UISceneActivationStateForegroundActive) { scene = (UIWindowScene*)sc; break; }
    if (!scene) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{ ShowMenu(); });
        return;
    }
    g_win = [[PGWin alloc] initWithWindowScene:scene];
    g_win.windowLevel = UIWindowLevelAlert + 1;
    g_win.backgroundColor = UIColor.clearColor;
    g_win.rootViewController = [PGVC new];
    g_win.hidden = NO;
}
#endif

__attribute__((constructor)) static void PGInit() {
    g_cfg = [@{@"guide": @1, @"obj": @1, @"cue": @1, @"bounces": @4} mutableCopy];
#if PG_MODE >= 1
    Class c = JSContext.class;
    Method m1 = class_getInstanceMethod(c, @selector(evaluateScript:));
    Method m2 = class_getInstanceMethod(c, @selector(evaluateScript:withSourceURL:));
    if (m1) g_eval1 = method_setImplementation(m1, (IMP)hook1);
    if (m2) g_eval2 = method_setImplementation(m2, (IMP)hook2);
#endif
#if PG_MODE >= 2
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ ShowMenu(); });
#endif
}
