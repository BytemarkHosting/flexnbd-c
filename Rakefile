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

# If you've made a commit, the changelog needs redoing
file "debian/changelog" => ".hg/last-message.txt" do
  latesttag_cmd = "hg log -l1 --template '{latesttag}'"
  latesttag = `#{latesttag_cmd}`.strip
  log_cmd = "hg log -r #{latesttag}:0 --style xml"
  projname = "flexnbd"

  File.open("debian/changelog", "w") do |changelog|
    IO.popen( log_cmd, "r" ) do |io|
      listener = HGChangelog::Listener.new changelog, projname
      REXML::Document.parse_stream io, listener
      io.close
    end
  end
end
task :changelog => "debian/changelog"

BEGIN {
  require 'rexml/document'
  require 'date'
  require 'erb'

  module HGChangelog
    class ChangelogEntry
      attr_accessor :email
      attr_accessor :author_name
      attr_accessor :date
      def initialize(projname, tag, rev)
        @projname = projname
        @tag = tag
        @rev = rev
        @lines = []
      end

      def close( stream )
        stream.write self.to_s
      end

      def <<( msg )
        @lines << msg
      end

      def to_s
        template = <<-TEMPLATE
<%= @projname %> (<%= @tag %>-<%= @rev %>) stable; urgency=low

<% for line in @lines -%>
  * <%= line.split("\n").first.strip %>
<% end -%>

 -- <%= @author_name %> <<%= @email %>>  <%= render_date %>

        TEMPLATE

        ERB.new(template, nil, "-").result(binding)
      end

      def render_date
        DateTime.parse(@date).rfc2822
      end

    end # class ChangelogEntry


    class Listener
      def initialize out, projname
        @out = out
        @projname = projname
      end

      def method_missing(sym,*args,&blk)
      end

      def tag_start( name, attrs )
        case name
        when "logentry"
          @rev = attrs['revision']
        when "author"
          @email = attrs['email']
        when "tag"
          @entry.close @out if @entry
          @tagged = true
        end
      end

      def tag_end( name )
        case name
        when "logentry"
          if @tagged
            @entry.email = @email
            @entry.author_name = @author_name
            @entry.date = @date
          end
          @entry << @message if @message !~ /Added tag.*for changeset/
                                                          @tagged = false
          when "author"
            @author_name = @text
          when "tag"
            @entry = ChangelogEntry.new @projname, @text, @rev
          when "msg"
            @message = @text
          when "date"
            @date = @text
          when "log"
            @entry.close @out
          end
        end

        def text( text )
          @text = text
        end
      end # class Listener
    end # module HGChangelog
}
