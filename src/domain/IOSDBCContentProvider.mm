#include "vehicle-sim/domain/IOSDBCContentProvider.h"
#import <Foundation/Foundation.h>

namespace vehicle_sim::domain {

std::string IOSDBCContentProvider::loadContent(const std::string& fileName) const {
    NSString* nsFileName = [NSString stringWithUTF8String:fileName.c_str()];
    NSString* bundlePath = [[NSBundle mainBundle] pathForResource:nsFileName ofType:nil];
    if (!bundlePath) {
        return "";
    }

    NSError* error = nil;
    NSString* content = [NSString stringWithContentsOfFile:bundlePath
                                                 encoding:NSUTF8StringEncoding
                                                    error:&error];
    if (error || !content) {
        return "";
    }

    return std::string([content UTF8String]);
}

} // namespace vehicle_sim::domain