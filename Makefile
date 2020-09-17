LUA ?= lua

getversion = $(LUA) scripts/getversion.lua
tidyfile = $(LUA) scripts/tidyfile.lua

rock_version = $(shell $(getversion))
rockspec = rockspecs/ldk-checks-$(rock_version)-1.rockspec
rockspec_dev = rockspecs/ldk-checks-dev-1.rockspec

.PHONY: rockspec spec docs

default: spec

docs: build-aux/config.ld
	ldoc -c build-aux/config.ld -t 'ldk-checks $(rock_version)' .
	$(tidyfile) docs/ldoc.css docs/index.html

lint: $(rockspec) $(rockspec-dev)
	luarocks lint $(rockspec)
	luarocks lint $(rockspec_dev)
	luacheck --quiet --formatter plain src spec

spec: build
	luarocks test

coverage: build
	luarocks test -- -c
	luacov
	luacov -r summary

build: $(rockspec-dev)
	luarocks make --local --no-install

publish: rockspec
	luarocks upload --temp-key=$(LDK_LUAROCKS_KEY) $(rockspec)

publish-force: rockspec
	luarocks upload --force --temp-key=$(LDK_LUAROCKS_KEY) $(rockspec)

changelog:
	git-chglog --output CHANGELOG.md --next-tag v$(rock_version)
	$(tidyfile) CHANGELOG.md

rockspec: $(rockspec_dev)
	luarocks new_version --dir rockspecs $(rockspec_dev) --tag v$(rock_version)
	$(tidyfile) $(wildcard rockspecs/*)

pre-release: rockspec docs changelog

bump-version:
	$(getversion) next

tag-release:
	git tag v$(rock_version)
