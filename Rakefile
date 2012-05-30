require 'rake_utils/debian'
include RakeUtils::DSL

DEBUG  = true

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

namespace "test" do
  desc "Run all tests"
  task 'run' => ["unit", "scenarios"]

  desc "Build C tests"
  task 'build' => TEST_MODULES.map { |n| "build/tests/check_#{n}" }

  desc "Run C tests"
  task 'unit' => 'build' do
    TEST_MODULES.each do |n|
      ENV['EF_DISABLE_BANNER'] = '1'
      sh "build/tests/check_#{n}"
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

TEST_MODULES.each do |m|
  deps = ["tests/check_#{m}.c", "build/util.o"]
  maybe_obj_name = "build/#{m}.o"

  deps << maybe_obj_name if OBJECTS.include?( maybe_obj_name )

  file "build/tests/check_#{m}" => deps do |t|
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
