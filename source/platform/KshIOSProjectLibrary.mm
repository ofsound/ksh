#include "KshIOSProjectLibrary.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <CommonCrypto/CommonDigest.h>
#include <cstdio>
#include <cerrno>

@interface KSHProjectPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property (nonatomic, copy) void (^picked) (NSURL*);
@end
@implementation KSHProjectPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    (void) controller;
    if (self.picked) self.picked (urls.firstObject);
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller
{
    (void) controller;
    if (self.picked) self.picked (nil);
}
@end

namespace ksh
{
namespace
{
   #if KSH_IOS_ICLOUD_ENABLED
    NSString* const containerID = @"iCloud.com.ofsound.ksh";
   #endif
    constexpr NSUInteger maximumProjectBytes = 16 * 1024 * 1024;
    NSString* ns (const juce::String& s) { return [NSString stringWithUTF8String:s.toRawUTF8()]; }
    juce::String string (NSString* s) { return s == nil ? juce::String() : juce::String::fromUTF8 (s.UTF8String); }
    juce::File file (NSURL* url) { return url == nil ? juce::File() : juce::File (string (url.path)); }
    NSURL* url (const juce::File& f) { return [NSURL fileURLWithPath:ns (f.getFullPathName())]; }
    juce::String digest (NSData* data)
    {
        if (data == nil) return {};
        unsigned char bytes[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256 (data.bytes, static_cast<CC_LONG> (data.length), bytes);
        return juce::String::toHexString (bytes, CC_SHA256_DIGEST_LENGTH, 0);
    }
    NSData* bytes (const juce::MemoryBlock& data) { return [NSData dataWithBytes:data.getData() length:data.getSize()]; }
    UIViewController* presenter (juce::Component& component)
    {
        auto* peer = component.getPeer();
        if (peer == nullptr) return nil;
        UIView* view = (__bridge UIView*) peer->getNativeHandle();
        auto* controller = view.window.rootViewController;
        while (controller.presentedViewController != nil)
            controller = controller.presentedViewController;
        return controller;
    }
    struct Access
    {
        NSURL* resource;
        bool active;
        explicit Access (NSURL* u) : resource (u), active ([u startAccessingSecurityScopedResource]) {}
        ~Access() { if (active) [resource stopAccessingSecurityScopedResource]; }
    };
}

struct IOSProjectLibrary::State : std::enable_shared_from_this<State>
{
    const juce::File localFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("KSH");
    juce::File library = localFolder;
    juce::Array<juce::File> cloudFiles;
    NSURL* currentURL = nil;
    NSURL* pendingURL = nil;
    juce::String baseline, pendingBaseline;
    NSMetadataQuery* query = nil;
    id gathered = nil, updated = nil, identityChanged = nil;
    UIDocumentPickerViewController* picker = nil;
    UIActivityViewController* activity = nil;
    KSHProjectPickerDelegate* delegate = nil;
    juce::File staging;
    dispatch_queue_t queue = dispatch_queue_create ("com.ofsound.ksh.projects", DISPATCH_QUEUE_SERIAL);
    Completion completion;
    bool ready = false;
    std::function<void()> whenReady;
    uint64_t generation = 0, discoveryGeneration = 0;

    ~State()
    {
        auto* centre = [NSNotificationCenter defaultCenter];
        if (gathered) [centre removeObserver:gathered];
        if (updated) [centre removeObserver:updated];
        if (identityChanged) [centre removeObserver:identityChanged];
        auto* oldQuery = query;
        dispatch_async (dispatch_get_main_queue(), ^{ [oldQuery stopQuery]; });
        staging.deleteRecursively();
    }

    void observeAccount()
    {
        std::weak_ptr<State> weak = shared_from_this();
        identityChanged = [[NSNotificationCenter defaultCenter] addObserverForName:NSUbiquityIdentityDidChangeNotification
            object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification*) {
                if (auto self = weak.lock())
                {
                    self->cancel();
                    self->currentURL = nil;
                    self->baseline.clear();
                    self->cloudFiles.clear();
                    [self->query stopQuery];
                    auto* centre = [NSNotificationCenter defaultCenter];
                    if (self->gathered) [centre removeObserver:self->gathered];
                    if (self->updated) [centre removeObserver:self->updated];
                    self->gathered = self->updated = nil;
                    self->query = nil;
                    self->ready = false;
                    self->initialise();
                }
            }];
    }

    void cancel (bool notify = true)
    {
        ++generation;
        whenReady = {};
        if (!notify) completion = {};
        delegate.picked = nil;
        [picker dismissViewControllerAnimated:YES completion:nil];
        [activity dismissViewControllerAnimated:YES completion:nil];
        finish ({ {}, {}, {}, true });
    }

    void initialise()
    {
        auto self = shared_from_this();
        const auto discovery = ++discoveryGeneration;
        dispatch_async (queue, ^{
            auto directory = self->localFolder;
           #if KSH_IOS_ICLOUD_ENABLED
            auto* manager = [NSFileManager defaultManager];
            NSURL* root = [manager URLForUbiquityContainerIdentifier:containerID];
            NSURL* documents = [root URLByAppendingPathComponent:@"Documents" isDirectory:YES];
            NSError* error = nil;
            const bool cloud = documents != nil && [manager createDirectoryAtURL:documents withIntermediateDirectories:YES attributes:nil error:&error];
            if (cloud) directory = file (documents);
           #endif
            self->localFolder.createDirectory();
            dispatch_async (dispatch_get_main_queue(), ^{
                if (self->discoveryGeneration != discovery) return;
                self->library = directory;
                self->ready = true;
               #if KSH_IOS_ICLOUD_ENABLED
                if (cloud) self->startQuery();
               #endif
                if (auto pending = std::exchange (self->whenReady, {})) pending();
            });
        });
    }

    void startQuery()
    {
        query = [[NSMetadataQuery alloc] init];
        query.searchScopes = @[NSMetadataQueryUbiquitousDocumentsScope];
        query.predicate = [NSPredicate predicateWithFormat:@"%K ENDSWITH[c] %@", NSMetadataItemFSNameKey, @".kshproject"];
        std::weak_ptr<State> weak = shared_from_this();
        auto changed = ^(NSNotification*) {
            if (auto self = weak.lock())
            {
                [self->query disableUpdates];
                self->cloudFiles.clear();
                for (NSMetadataItem* item in self->query.results)
                {
                    auto f = file ([item valueForAttribute:NSMetadataItemURLKey]);
                    if (f.isAChildOf (self->library)) self->cloudFiles.add (f);
                }
                [self->query enableUpdates];
            }
        };
        auto* centre = [NSNotificationCenter defaultCenter];
        gathered = [centre addObserverForName:NSMetadataQueryDidFinishGatheringNotification object:query queue:[NSOperationQueue mainQueue] usingBlock:changed];
        updated = [centre addObserverForName:NSMetadataQueryDidUpdateNotification object:query queue:[NSOperationQueue mainQueue] usingBlock:changed];
        [query startQuery];
    }

    bool begin (Completion done)
    {
        if (completion)
        {
            done ({ {}, {}, "A project operation is already open." });
            return false;
        }
        pendingURL = nil;
        pendingBaseline.clear();
        completion = std::move (done);
        ++generation;
        return true;
    }

    void finish (Result result)
    {
        delegate.picked = nil;
        delegate = nil;
        picker = nil;
        activity = nil;
        staging.deleteRecursively();
        staging = juce::File();
        if (auto done = std::exchange (completion, {})) done (std::move (result));
    }

    void performWhenReady (std::function<void()> action)
    {
        if (ready) action();
        else whenReady = std::move (action);
    }

    static Result readData (NSURL* selected, juce::String* fingerprint = nullptr)
    {
        Access access (selected);
        NSError* error = nil;
        NSNumber* ubiquitous = nil;
        [selected getResourceValue:&ubiquitous forKey:NSURLIsUbiquitousItemKey error:nil];
        if (ubiquitous.boolValue)
            [[NSFileManager defaultManager] startDownloadingUbiquitousItemAtURL:selected error:&error];
        __block NSData* data = nil;
        __block NSString* failure = nil;
        auto* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
        [coordinator coordinateReadingItemAtURL:selected options:0 error:&error byAccessor:^(NSURL* coordinated) {
            NSNumber* size = nil;
            [coordinated getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
            if (size == nil || size.unsignedLongLongValue == 0 || size.unsignedLongLongValue > maximumProjectBytes)
                failure = @"The selected project is empty or too large.";
            else
                data = [NSData dataWithContentsOfURL:coordinated options:0 error:nil];
        }];
        Result result;
        result.file = file (selected);
        if (error != nil || data == nil)
            result.error = failure != nil ? string (failure) : "The project could not be downloaded or read. Check your connection and try again.";
        else
        {
            result.data = juce::MemoryBlock (data.bytes, data.length);
            if (fingerprint != nullptr)
                *fingerprint = digest (data);
        }
        return result;
    }

    void readURL (NSURL* selected)
    {
        auto self = shared_from_this();
        const auto operation = generation;
        dispatch_async (queue, ^{
            juce::String fingerprint;
            auto result = readData (selected, &fingerprint);
            dispatch_async (dispatch_get_main_queue(), ^{
                if (self->generation != operation) return;
                if (result.error.isEmpty())
                {
                    self->pendingURL = selected;
                    self->pendingBaseline = fingerprint;
                }
                self->finish (result);
            });
        });
    }

    void showOpenPicker (juce::Component& component)
    {
        auto* parent = presenter (component);
        if (parent == nil) { finish ({ {}, {}, "The Files picker could not be opened." }); return; }
        auto* type = [UTType typeWithIdentifier:@"com.ofsound.ksh.project"];
        picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[type != nil ? type : UTTypeData] asCopy:NO];
        picker.directoryURL = url (library);
        picker.allowsMultipleSelection = NO;
        delegate = [[KSHProjectPickerDelegate alloc] init];
        std::weak_ptr<State> weak = shared_from_this();
        delegate.picked = ^(NSURL* selected) {
            auto self = weak.lock();
            if (!self) return;
            if (selected == nil) { self->finish ({ {}, {}, {}, true }); return; }
            self->readURL (selected);
        };
        picker.delegate = delegate;
        [parent presentViewController:picker animated:YES completion:nil];
    }
};

IOSProjectLibrary::IOSProjectLibrary() : state (std::make_shared<State>())
{
   #if KSH_IOS_ICLOUD_ENABLED
    state->observeAccount();
   #endif
    state->initialise();
}
IOSProjectLibrary::~IOSProjectLibrary() = default;
juce::File IOSProjectLibrary::directory() const { return state->library; }
juce::File IOSProjectLibrary::localDirectory() const { return state->localFolder; }
juce::File IOSProjectLibrary::currentFile() const { return file (state->currentURL); }
bool IOSProjectLibrary::busy() const { return static_cast<bool> (state->completion); }
juce::Array<juce::File> IOSProjectLibrary::files (const juce::File& directory) const
{
    juce::Array<juce::File> result;
    for (const auto& f : state->cloudFiles)
        if (f.getParentDirectory() == directory) result.add (f);
    for (const auto& f : directory.findChildFiles (juce::File::findFiles | juce::File::ignoreHiddenFiles, false, "*.kshproject"))
        result.addIfNotAlreadyThere (f);
    return result;
}
void IOSProjectLibrary::cancel (bool notify) { state->cancel (notify); }
void IOSProjectLibrary::acceptFile (const juce::File& accepted)
{
    // Only the editor can accept a fully validated load or a save of the same document epoch.
    if (state->pendingURL != nil && file (state->pendingURL) == accepted)
    {
        state->currentURL = state->pendingURL;
        state->baseline = state->pendingBaseline;
        state->pendingURL = nil;
        state->pendingBaseline.clear();
    }
}
void IOSProjectLibrary::chooseOpen (juce::Component& component, Completion done)
{
    if (!state->begin (std::move (done))) return;
    auto self = state;
    juce::Component::SafePointer<juce::Component> safe (&component);
    state->performWhenReady ([self, safe] {
        if (safe == nullptr) self->finish ({ {}, {}, {}, true });
        else self->showOpenPicker (*safe);
    });
}
void IOSProjectLibrary::read (const juce::File& f, Completion done)
{
    if (state->begin (std::move (done)))
        state->readURL (file (state->currentURL) == f ? state->currentURL : url (f));
}
void IOSProjectLibrary::readPreview (const juce::File& f, Completion done)
{
    auto* selected = file (state->currentURL) == f ? state->currentURL : url (f);
    dispatch_async (dispatch_get_global_queue (QOS_CLASS_UTILITY, 0), ^{
        auto result = State::readData (selected);
        dispatch_async (dispatch_get_main_queue(), ^{ done (result); });
    });
}
void IOSProjectLibrary::save (const juce::File& f, juce::MemoryBlock data, bool createOnly, Completion done)
{
    if (!state->begin (std::move (done))) return;
    auto self = state;
    auto* payload = bytes (data);
    if (!createOnly && (file (state->currentURL) != f || state->baseline.isEmpty()))
    {
        state->finish ({ {}, {}, "Reopen this project before saving, or use Save As to keep a copy." });
        return;
    }
    auto* selected = createOnly ? url (f) : state->currentURL;
    const auto expected = state->baseline;
    const auto operation = state->generation;
    const bool local = f.isAChildOf (state->localFolder);
    const auto destination = f;
    dispatch_async (state->queue, ^{
        Result result;
        result.file = destination;
        if (createOnly)
            result.error = destination.getParentDirectory().createDirectory().getErrorMessage();
        __block bool saved = false;
        __block juce::String failure;
        auto write = ^(NSURL* target) {
            if (createOnly)
            {
                const auto targetFile = file (target);
                juce::TemporaryFile temporary (targetFile);
                const auto& staged = temporary.getFile();
                if (![payload writeToURL:url (staged) options:0 error:nil])
                    return;
                saved = ::renamex_np (staged.getFullPathName().toRawUTF8(), targetFile.getFullPathName().toRawUTF8(), RENAME_EXCL) == 0;
                if (!saved && errno == EEXIST)
                    failure = "That project name is already in use.";
                return;
            }
            NSNumber* size = nil;
            [target getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
            auto* previous = size != nil && size.unsignedLongLongValue <= maximumProjectBytes
                               ? [NSData dataWithContentsOfURL:target options:0 error:nil] : nil;
            if ((!local && [NSFileVersion unresolvedConflictVersionsOfItemAtURL:target].count > 0) || digest (previous) != expected)
            {
                failure = "This project changed on another device or app. Use Save As to keep your edits, then reopen the original.";
                return;
            }
            saved = [payload writeToURL:target options:NSDataWritingAtomic error:nil];
        };
        if (result.error.isEmpty())
        {
            NSError* error = nil;
            if (local)
                write (selected);
            else
            {
                Access access (selected);
                auto* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
                const auto options = createOnly ? NSFileCoordinatorWritingOptions (0) : NSFileCoordinatorWritingForReplacing;
                [coordinator coordinateWritingItemAtURL:selected options:options error:&error byAccessor:write];
            }
            if (!saved)
                result.error = failure.isNotEmpty() ? failure
                    : error != nil ? string (error.localizedDescription) : "The project could not be saved. Check file access or use Save As.";
        }
        dispatch_async (dispatch_get_main_queue(), ^{
            if (self->generation != operation) return;
            if (result.error.isEmpty())
            {
                self->pendingURL = selected;
                self->pendingBaseline = digest (payload);
            }
            self->finish (result);
        });
    });
}
void IOSProjectLibrary::share (juce::Component& component, const juce::String& name, juce::MemoryBlock data, Completion done)
{
    if (!state->begin (std::move (done))) return;
    auto* parent = presenter (component);
    if (parent == nil) { state->finish ({ {}, {}, "The share sheet could not be opened." }); return; }
    state->staging = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("KSH-share", {}, false);
    auto staged = state->staging.getChildFile (juce::File::createLegalFileName (name).isEmpty() ? "Untitled Project" : juce::File::createLegalFileName (name)).withFileExtension ("kshproject");
    if (state->staging.createDirectory().failed() || !staged.replaceWithData (data.getData(), data.getSize()))
    { state->finish ({ {}, {}, "The project copy could not be written." }); return; }
    state->activity = [[UIActivityViewController alloc] initWithActivityItems:@[url (staged)] applicationActivities:nil];
    state->activity.popoverPresentationController.sourceView = parent.view;
    state->activity.popoverPresentationController.sourceRect = CGRectMake (parent.view.bounds.size.width / 2, parent.view.bounds.size.height / 2, 1, 1);
    std::weak_ptr<State> weak = state;
    const auto operation = state->generation;
    state->activity.completionWithItemsHandler = ^(UIActivityType, BOOL completed, NSArray*, NSError* error) {
        if (auto self = weak.lock(); self && self->generation == operation)
            self->finish ({ {}, {}, string (error.localizedDescription), !completed && error == nil });
    };
    [parent presentViewController:state->activity animated:YES completion:nil];
}
void IOSProjectLibrary::restoreAccess (const juce::PropertySet& settings)
{
    juce::MemoryBlock bookmark;
    if (!bookmark.fromBase64Encoding (settings.getValue ("projectBookmark"))) return;
    BOOL stale = NO;
    auto* restored = [NSURL URLByResolvingBookmarkData:bytes (bookmark) options:0 relativeToURL:nil bookmarkDataIsStale:&stale error:nil];
    if (restored != nil)
    {
        state->currentURL = restored;
        state->baseline = settings.getValue ("projectContentDigest");
    }
}
void IOSProjectLibrary::saveAccess (juce::PropertySet& settings) const
{
    Access access (state->currentURL);
    auto* bookmark = [state->currentURL bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:nil];
    settings.setValue ("projectBookmark", bookmark == nil ? juce::String() : juce::MemoryBlock (bookmark.bytes, bookmark.length).toBase64Encoding());
    settings.setValue ("projectContentDigest", state->baseline);
}
}
