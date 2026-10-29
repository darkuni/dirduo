#pragma once

// Gives the thumbnail cache what only the UI library has: PDF pages (Poppler
// or Qt PDF) and video frames (Qt Multimedia), and keeps its options in step
// with Preferences > Thumbnails. Safe to call more than once.
namespace ThumbMakers {
void install();
bool canDoVideo();
bool canDoDocuments();
} // namespace ThumbMakers
