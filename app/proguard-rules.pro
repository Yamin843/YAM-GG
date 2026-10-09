-keep class com.yamgg.modview.** { *; }
-keepclassmembers class com.yamgg.modview.** { *; }
-keepclasseswithmembernames class com.yamgg.modview.** {
    native <methods>;
}
-dontwarn java.lang.**
-dontwarn javax.**
-dontwarn android.**
-keepattributes SourceFile,LineNumberTable
-keepattributes *Annotation*
-keepattributes Signature
-keepattributes Exceptions
