.PHONY: all release debug run_poco clean

all: debug

debug:
	rm -rf build && \
	mkdir -p _build && \
	cd _build && \
	pixi run cmake -G "Ninja" -DWITH_POCO=ON .. && \
	pixi run cmake --build . --config debug --target install && \
	cd ..

release:
	rm -rf _build && \
	mkdir -p _build && \
	cd _build && \
	pixi run cmake -G "Ninja" -DWITH_POCO=ON .. && \
	pixi run cmake --build . --config RelWithDebInfo --target install && \
	cd ..

run:
	@cd _build/bin &&\
	./ani

lldb:
	@cd _build/bin && lldb ani --batch \
		 --one-line 'process launch' \
		 --one-line-on-crash 'bt' \
	     --one-line-on-crash 'quit'

run_poco:
	lldb -o run -o bt -o exit _build/bin/poco poco/test/po_ffi_01.poc

clean:
	rm -rf _build && \
	echo Project Cleaned

