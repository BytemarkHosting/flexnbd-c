# encoding: utf-8

def make(*targets)
  sh "make #{targets.map{|t| t.to_s}.join(" ")}"
end

def maketask( opts )
  case opts
  when Symbol
    maketask opts => opts
  else
    opts.each do |name, targets|
      task( name ){make *[*targets]}
    end
  end
end


desc "Build the binary and man page"
maketask :build => [:all, :doc]

desc "Build just the flexnbd binary"
maketask :flexnbd => [:server]
file "build/flexnbd" => :flexnbd

desc "Build just the flexnbd-proxy binary"
maketask :flexnbd_proxy => [:proxy]
file "build/flexnbd-proxy" => :flexnbd_proxy

desc "Build just the man page"
maketask :man => :doc


namespace "test" do
  desc "Run all tests"
  task 'run' => ["unit", "scenarios"]

  desc "Build C tests"
  maketask :build => :check_bins

  desc "Run C tests"
  maketask :unit => :check

  desc "Run NBD test scenarios"
  task 'scenarios' => ["build/flexnbd", "build/flexnbd-proxy"] do
    sh "cd tests/acceptance && RUBYOPT='-I.' ruby nbd_scenarios -v"
  end
end


desc "Remove all build targets, binaries and temporary files"
maketask :clean
