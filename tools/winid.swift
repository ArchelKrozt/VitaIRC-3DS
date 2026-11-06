// Prints "id width height title" for the on-screen windows of an app (for screencapture -l).
import CoreGraphics
let want = CommandLine.arguments.count > 1 ? CommandLine.arguments[1].lowercased() : "azahar"
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]
for w in list {
  let owner = w[kCGWindowOwnerName as String] as? String ?? ""
  if owner.lowercased().contains(want) {
    let b = w[kCGWindowBounds as String] as! [String: Any]
    print(w[kCGWindowNumber as String]!, b["Width"]!, b["Height"]!, w[kCGWindowName as String] ?? "")
  }
}
