local function open_file(filename, mode)
  local file, err = io.open(filename, mode)
  if err then
    error(err)
  end

  return file
end

local function read_file(filename)
  local file = open_file(filename, 'r')
  local text, err = file:read('a')
  if err then
    error(err)
  end

  file:close()
  return text:match('^%s*(.-)%s*$')
end

local function write_file(filename, text)
  local file = open_file(filename, 'w+b')
  local _, err = file:write(text, '\n')
  if err then
    error(err)
  end

  file:close()
end

local function fail(fmt, ...)
  io.stderr:write(fmt:format(...), '\n')
  os.exit(1)
end

local function format_version(version)
  return ('%d.%d.%d'):format(version.major, version.minor, version.patch)
end

local function parse_version(s)
  local major, minor, patch = s:match('^(%d+)%.(%d+)%.(%d+)')
  if not major then
    fail("invalid version format: %q", s)
  end

  if major then
    major, minor, patch = tonumber(major), tonumber(minor), tonumber(patch)
    if not major or not minor or not patch then
      fail("invalid version format: %q", s)
    end

    return {major = major, minor = minor, patch = patch }
  end
end

local VERSION_RC = '.versionrc'

local function read_version()
  return parse_version(read_file(VERSION_RC))
end

local function write_version(version)
  write_file(VERSION_RC, format_version(version))
end

local function next_version(what)
  what = what or 'patch'

  local version = read_version()
  if not version then
    version = { major = 0, minor = 1, patch = 0 }
  end

  if not version[what] then
    fail("invalid argument '%s'", what)
  end

  version[what] = version[what] + 1
  if what == 'minor' then
    version.patch = 0
  elseif what == 'major' then
    version.minor = 0
    version.patch = 0
  end

  write_version(version)
  print("new version: " .. format_version(version))
end

local function print_version()
  local version = read_version()
  if not version then
    fail("initialize the project first")
  end

  print(format_version(version))
end

local function reset_version(version)
  version = version
    and parse_version(version)
    or { major = 0, minor = 1, patch = 0 }
  write_version(version)
  print("new version: " .. format_version(version))
end

local function run(name, ...)
  if not name then
    print_version()
  elseif name == 'reset' then
    reset_version(...)
  elseif name == 'next' then
    next_version(...)
  else
    fail("unknown command '%s'", name)
  end
end

run(...)
