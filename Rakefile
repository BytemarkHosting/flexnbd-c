$: << '../rake_utils/lib'
require 'rake_utils/debian'
include RakeUtils::DSL

CC=ENV['CC'] || "gcc"

DEBUG  = ENV.has_key?('DEBUG') &&
  %w|yes y ok 1 true t|.include?(ENV['DEBUG'])

ALL_SOURCES =FileList['src/*']
SOURCES = ALL_SOURCES.select { |c| c =~ /\.c$/ }
OBJECTS = SOURCES.pathmap( "%{^src,build}X.o" )
TEST_SOURCES = FileList['tests/unit/*.c']
TEST_OBJECTS = TEST_SOURCES.pathmap( "%{^tests/unit,build/tests}X.o" )

LIBS    = %w( pthread )
CCFLAGS = %w(
             -D_GNU_SOURCE=1
             -Wall
             -Wextra
             -Werror-implicit-function-declaration
             -Wstrict-prototypes
             -Wno-missing-field-initializers
            ) + # Added -Wno-missing-field-initializers to shut GCC up over {0} struct initialisers
            [ENV['CFLAGS']]
LDFLAGS = []
LIBCHECK = "/usr/lib/libcheck.a"

TEST_MODULES = Dir["tests/unit/check_*.c"].map { |n|
  File.basename( n )[%r{check_(.+)\.c},1] }

if DEBUG
  LDFLAGS << ["-g"]
  CCFLAGS << ["-g -DDEBUG"]
end

desc "Build the binary and man page"
task :build => ['build/flexnbd', 'build/flexnbd.1.gz']
task :default => :build

desc "Build just the binary"
task :flexnbd => "build/flexnbd"

def check(m)
  "build/tests/check_#{m}"
end

file "README.txt"

file "build/flexnbd.1.gz" => "README.txt" do
  FileUtils.mkdir_p( "build" )
  sh "a2x --destination-dir build --format manpage README.txt"
  sh "gzip -f build/flexnbd.1"
end

desc "Build just the man page"
task :man => "build/flexnbd.1.gz"


namespace "test" do
  desc "Run all tests"
  task 'run' => ["unit", "scenarios"]

  desc "Build C tests"
  task 'build' => TEST_MODULES.map { |n| check n}

  TEST_MODULES.each do |m|
    desc "Run tests for #{m}"
    task "check_#{m}" => check(m) do
      sh check m
    end
  end

  desc "Run C tests"
  task 'unit' => 'build' do
    TEST_MODULES.each do |n|
      ENV['EF_DISABLE_BANNER'] = '1'
      sh check n
    end
  end

  desc "Run NBD test scenarios"
  task 'scenarios' => 'flexnbd' do
    sh "cd tests/acceptance; ruby nbd_scenarios"
  end
end



def gcc_compile( target, source )
  FileUtils.mkdir_p File.dirname( target )
  sh "#{CC} -Isrc -c #{CCFLAGS.join(' ')} -o #{target} #{source} "
end

def gcc_link(target, objects)
  FileUtils.mkdir_p File.dirname( target )

  sh "#{CC} #{LDFLAGS.join(' ')} "+
    LIBS.map { |l| "-l#{l}" }.join(" ")+
    " -Isrc " +
    " -o #{target} "+
    objects.join(" ")
end

def headers(c)
  `#{CC} -Isrc -MM #{c}`.gsub("\\\n", " ").split(" ")[2..-1]
end

rule 'build/flexnbd' => OBJECTS do |t|
  gcc_link(t.name, t.sources)
end


file check("client") =>
%w{build/tests/check_client.o
  build/self_pipe.o
  build/nbdtypes.o
  build/flexnbd.o
  build/flexthread.o
  build/control.o
  build/readwrite.o
  build/parse.o
  build/client.o
  build/serve.o
  build/acl.o
  build/ioutil.o
  build/mbox.o
  build/mirror.o
  build/status.o
  build/util.o} do |t|
  gcc_link t.name, t.prerequisites + [LIBCHECK]
end

file check("acl") =>
%w{build/tests/check_acl.o
  build/parse.o
  build/acl.o
  build/util.o} do |t|
  gcc_link t.name, t.prerequisites + [LIBCHECK]
end

file check( "util" ) =>
%w{build/tests/check_util.o
  build/util.o
  build/self_pipe.o} do |t|
  gcc_link t.name, t.prerequisites + [LIBCHECK]
end

file check("serve") =>
%w{build/tests/check_serve.o
  build/self_pipe.o
  build/nbdtypes.o
  build/control.o
  build/readwrite.o
  build/parse.o
  build/client.o
  build/flexthread.o
  build/serve.o
  build/flexnbd.o
  build/mirror.o
  build/status.o
  build/acl.o
  build/mbox.o
  build/ioutil.o
  build/util.o} do |t|
  gcc_link t.name, t.prerequisites + [LIBCHECK]
end

file check("readwrite") =>
%w{build/tests/check_readwrite.o
  build/readwrite.o
  build/client.o
  build/self_pipe.o
  build/serve.o
  build/parse.o
  build/acl.o
  build/flexthread.o
  build/control.o
  build/flexnbd.o
  build/mirror.o
  build/status.o
  build/nbdtypes.o
  build/mbox.o
  build/ioutil.o
  build/util.o} do |t|
  gcc_link t.name, t.prerequisites + [LIBCHECK]
end


file check("flexnbd") =>
%w{build/tests/check_flexnbd.o
  build/flexnbd.o
  build/ioutil.o
  build/util.o
  build/control.o
  build/mbox.o
  build/flexthread.o
  build/status.o
  build/self_pipe.o
  build/client.o
  build/acl.o
  build/parse.o
  build/nbdtypes.o
  build/readwrite.o
  build/mirror.o
  build/serve.o} do |t|
  gcc_link t.name, t.prerequisites + [LIBCHECK]
end

file check("control") =>
  %w{build/tests/check_control.o} + OBJECTS - ["build/main.o"] do |t|
  gcc_link t.name, t.prerequisites + [LIBCHECK]
end

(TEST_MODULES- %w{control flexnbd acl client serve readwrite util}).each do |m|
  tgt = "build/tests/check_#{m}.o"
  maybe_obj_name = "build/#{m}.o"
  # Take it out in case we're testing util.o or ioutil.o
  deps = ["build/ioutil.o", "build/util.o"] - [maybe_obj_name]

  # Add it back in if it's something we need to compile
  deps << maybe_obj_name if OBJECTS.include?( maybe_obj_name )

  file check( m ) => deps + [tgt] do |t|
    gcc_link(t.name, deps + [tgt, LIBCHECK])
  end
end


OBJECTS.zip( SOURCES ).each do |o,c|
  file o => [c]+headers(c) do |t| gcc_compile( o, c ) end
end

TEST_OBJECTS.zip( TEST_SOURCES ).each do |o,c|
  file o => [c] + headers(c) do |t| gcc_compile( o, c ) end
end

desc "Remove all build targets, binaries and temporary files"
task :clean do
  sh "rm -rf *~ build"
end

namespace :pkg do
  deb do |t|
    t.code_files = ALL_SOURCES + ["Rakefile", "README.txt"]
    t.pkg_name = "flexnbd"
    t.generate_changelog!
  end
end

