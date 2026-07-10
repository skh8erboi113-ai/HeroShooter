# Makefile — convenience wrapper around Gradle for command-line builds
# Usage: make debug | make release | make install | make clean | make symbols

NDK_VERSION := 27.1.12297006
ABI         := arm64-v8a

.PHONY: debug release install clean symbols lint test

debug:
	@echo "==> Building DEBUG APK (arm64-v8a)"
	./gradlew assembleDebug \
		-Pandroid.injected.build.abi=$(ABI) \
		--parallel \
		--build-cache
	@echo "==> APK: app/build/outputs/apk/debug/app-arm64-v8a-debug.apk"

release:
	@echo "==> Building RELEASE APK"
	@test -n "$(KEYSTORE)" || (echo "ERROR: set KEYSTORE, KEYSTORE_PASS, KEY_ALIAS, KEY_PASS" && exit 1)
	./gradlew assembleRelease \
		-Pandroid.injected.signing.store.file=$(KEYSTORE) \
		-Pandroid.injected.signing.store.password=$(KEYSTORE_PASS) \
		-Pandroid.injected.signing.key.alias=$(KEY_ALIAS) \
		-Pandroid.injected.signing.key.password=$(KEY_PASS) \
		--parallel

bundle:
	@echo "==> Building Release AAB for Play Store"
	./gradlew bundleRelease

install: debug
	@echo "==> Installing to connected device"
	./gradlew installDebug
	adb shell am start -n com.heroshooter.engine/.MainActivity

# Extract and symbolicate native crash addresses
symbols:
	@echo "==> Pulling tombstones from device"
	adb pull /data/tombstones ./tombstones 2>/dev/null || true
	@LLVM_SYMBOLIZER=$(shell find $(ANDROID_NDK_HOME) -name "llvm-symbolizer" | head -1); \
	ndk-stack \
		-sym app/build/intermediates/cmake/debug/obj/$(ABI) \
		-dump ./tombstones/tombstone_00 \
		| head -80

# Run static analysis via Android Lint
lint:
	./gradlew lint --continue

# Run native unit tests on device
test:
	./gradlew connectedAndroidTest

# Print .so section sizes (useful for binary size optimisation)
size:
	@SO=app/build/intermediates/stripped_native_libs/release/out/lib/$(ABI)/libhero_shooter_engine.so; \
	$(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-size $$SO; \
	echo ""; \
	echo "Top 20 largest symbols:"; \
	$(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm \
		--print-size --size-sort --radix=d $$SO 2>/dev/null | tail -20

clean:
	./gradlew clean
	rm -rf app/.cxx
