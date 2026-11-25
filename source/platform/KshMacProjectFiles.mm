#include "KshMacProjectFiles.h"
#import <Foundation/Foundation.h>

namespace ksh
{
    namespace
    {
        NSURL* fileURL (const juce::File& file)
        {
            return [NSURL fileURLWithFileSystemRepresentation:file.getFullPathName().toRawUTF8() isDirectory:NO relativeToURL:nil];
        }

        juce::File fileFromURL (NSURL* url)
        {
            return url == nil ? juce::File() : juce::File (juce::String::fromUTF8 (url.fileSystemRepresentation));
        }
    }

    juce::File MacProjectFiles::documentsDirectory()
    {
        auto* url = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
        return url != nil ? fileFromURL (url) : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }

    juce::String MacProjectFiles::makeBookmark (const juce::File& file)
    {
        if (file == juce::File())
            return {};

        auto* bookmark = [fileURL (file) bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope
                                  includingResourceValuesForKeys:nil relativeToURL:nil error:nil];
        return bookmark == nil ? juce::String() : juce::MemoryBlock (bookmark.bytes, bookmark.length).toBase64Encoding();
    }

    juce::File MacProjectFiles::restoreBookmark (const juce::String& encoded)
    {
        scopedURL.reset();
        scopedFile = juce::File();
        juce::MemoryBlock bookmark;
        if (encoded.isEmpty() || !bookmark.fromBase64Encoding (encoded))
            return {};

        auto* data = [NSData dataWithBytes:bookmark.getData() length:bookmark.getSize()];
        BOOL stale = NO;
        auto* resolved = [NSURL URLByResolvingBookmarkData:data
                                                  options:NSURLBookmarkResolutionWithSecurityScope | NSURLBookmarkResolutionWithoutUI
                                            relativeToURL:nil bookmarkDataIsStale:&stale error:nil];
        if (resolved == nil || ![resolved startAccessingSecurityScopedResource])
            return {};

        scopedURL = { (__bridge_retained void*) resolved, [] (void* handle)
        {
            auto* url = (__bridge_transfer NSURL*) handle;
            [url stopAccessingSecurityScopedResource];
        } };
        scopedFile = fileFromURL (resolved);
        if (!scopedFile.existsAsFile())
            releaseAccessUnless ({});
        return scopedFile;
    }

    void MacProjectFiles::releaseAccessUnless (const juce::File& file)
    {
        if (file == scopedFile)
            return;
        scopedURL.reset();
        scopedFile = juce::File();
    }

    juce::Result MacProjectFiles::replace (const juce::File& file, const juce::MemoryBlock& data)
    {
        auto* manager = [NSFileManager defaultManager];
        auto* payload = [NSData dataWithBytes:data.getData() length:data.getSize()];
        auto* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
        NSError* coordinationError = nil;
        __block NSError* writeError = nil;
        __block BOOL saved = NO;
        [coordinator coordinateWritingItemAtURL:fileURL (file) options:NSFileCoordinatorWritingForReplacing
                                          error:&coordinationError byAccessor:^(NSURL* destination)
        {
            // A file-only grant does not cover an adjacent temp file. Foundation
            // supplies an accessible staging directory on the destination volume.
            auto* staging = [manager URLForDirectory:NSItemReplacementDirectory inDomain:NSUserDomainMask
                                     appropriateForURL:destination create:YES error:&writeError];
            if (staging == nil)
                return;
            auto* candidate = [staging URLByAppendingPathComponent:@"Project.kshproject"];
            saved = [payload writeToURL:candidate options:0 error:&writeError]
                 && [manager replaceItemAtURL:destination withItemAtURL:candidate backupItemName:nil
                                       options:0 resultingItemURL:nil error:&writeError];
            [manager removeItemAtURL:staging error:nil];
        }];

        if (saved && coordinationError == nil)
            return juce::Result::ok();
        const auto description = juce::String::fromUTF8 ((coordinationError != nil ? coordinationError : writeError).localizedDescription.UTF8String);
        return juce::Result::fail (description.isNotEmpty() ? description : "The project file could not be written.");
    }
}
