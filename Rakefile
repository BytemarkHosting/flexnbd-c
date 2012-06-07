require 'rake_utils/debian'
include RakeUtils::DSL

DEBUG  = ENV.has_key?('DEBUG') &&
  %w|yes y ok 1 true t|.include?(ENV['DEBUG'])

ALL_SOURCES =FileList['src/*']
SOURCES = ALL_SOURCES.select { |c| c =~ /\.c$/ }
OBJECTS = SOURCES.pathmap( "%{^src,build}X.o" )

LIBS    = %w( pthread )
CCFLAGS = %w( -Wall )
LDFLAGS = []
LIBCHECK = "/usr/lib/libcheck.a"

TEST_MODULES = Dir["tests/check_*.c"].map { |n| n[12..-3] }

if DEBUG
  LDFLAGS << ["-g"]
  CCFLAGS << ["-g -DDEBUG"]
end

desc "Build flexnbd binary"
task :flexnbd => 'build/flexnbd'
task :build => :flexnbd
task :default => :flexnbd

def check(m)
  "build/tests/check_#{m}"
end

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
    sh "cd tests; ruby nbd_scenarios"
  end
end


def gcc_link(target, objects)
  FileUtils.mkdir_p File.dirname( target )

  sh "gcc #{LDFLAGS.join(' ')} "+
    LIBS.map { |l| "-l#{l}" }.join(" ")+
    " -I src" +
    " -o #{target} "+
    objects.join(" ")
end

rule 'build/flexnbd' => OBJECTS do |t|
  gcc_link(t.name, t.sources)
end


file check("client") => 
%w{tests/check_client.c
  build/self_pipe.o
  build/nbdtypes.o
  build/control.o
  build/readwrite.o
  build/parse.o
  build/client.o
  build/serve.o 
  build/acl.o
  build/ioutil.o 
  build/util.o} do |t|
  gcc_link t.name, t.prerequisites + [LIBCHECK]
end

file check("acl") =>
%w{tests/check_acl.c
  build/parse.o
  build/acl.o
  build/util.o} do |t| 
  gcc_link t.name, t.prerequisites + [LIBCHECK]
end


(TEST_MODULES- %w{acl client}).each do |m|
  deps = ["tests/check_#{m}.c", "build/ioutil.o", "build/util.o"]
  maybe_obj_name = "build/#{m}.o"

  deps << maybe_obj_name if OBJECTS.include?( maybe_obj_name )

  file check( m ) => deps do |t|
    gcc_link(t.name, deps + [LIBCHECK])
  end
end


OBJECTS.zip( SOURCES ).each do |o,c|
  file o => c do |t|
    FileUtils.mkdir_p File.dirname( o )
    sh "gcc -Isrc -c #{CCFLAGS.join(' ')} -o #{o} #{c} "
  end
end

desc "Remove all build targets, binaries and temporary files"
task :clean do
  sh "rm -rf *~ build"
end

namespace :pkg do
  deb do |t|
    t.code_files = ALL_SOURCES + ["Rakefile"]
    t.pkg_name = "flexnbd"
    t.generate_changelog!
  end
end

