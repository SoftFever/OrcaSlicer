<?xml version="1.0" encoding='ISO-8859-1' ?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<!--
 * Copyright (c) 2006 Joerg Wunsch
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * $Id$
-->
<!--
 * Extract the debugWire parameters
 * from the XML, and format it the way src/devdescr.cc needs it.
 *
 * Run this file together with the respective AVR's XML file through
 * an XSLT processor (xsltproc, saxon), and capture the output for
 * inclusion into avrdude.conf.in.
-->
    <xsl:output method="text"/>
    <xsl:template match="/">
      <!-- Extract everything we need out of the XML. -->
      <xsl:variable name="devname_orig"
                    select="/AVRPART/ADMIN/PART_NAME" />
      <xsl:variable name="devname"
                    select="translate(/AVRPART/ADMIN/PART_NAME,
                                      'abcdefghijklmnopqrstuvwxyz',
                                      'ABCDEFGHIJKLMNOPQRSTUVWXYZ')" />
      <xsl:variable name="devname_lower"
                    select="translate(/AVRPART/ADMIN/PART_NAME,
                                      'ABCDEFGHIJKLMNOPQRSTUVWXYZ',
                                      'abcdefghijklmnopqrstuvwxyz')" />
      <xsl:variable name="ucEepromInst"
                    select="//AVRPART/ICE_SETTINGS/JTAGICEmkII/ucEepromInst" />
      <xsl:variable name="ucFlashInst"
                    select="//AVRPART/ICE_SETTINGS/JTAGICEmkII/ucFlashInst" />

      <!-- If there's a JTAGICEmkII node indicating debugWire, emit the entry. -->
      <xsl:if test='//AVRPART/ICE_SETTINGS/JTAGICEmkII/Interface="DebugWire"'>

      <!-- start of new entry -->
      <xsl:text>#------------------------------------------------------------&#010;</xsl:text>
      <xsl:text># </xsl:text>
      <xsl:value-of select="$devname_orig" />
      <xsl:text>&#010;</xsl:text>
      <xsl:text>#------------------------------------------------------------&#010;</xsl:text>
      <xsl:text>part&#010;     desc          = &quot;</xsl:text>
      <xsl:value-of select="$devname_orig" />
      <xsl:text>&quot;;&#010;     has_debugwire = yes;&#010;</xsl:text>

      <xsl:text>     flash_instr   = </xsl:text>
      <xsl:call-template name="format-hex">
        <xsl:with-param name="arg" select="$ucFlashInst" />
        <xsl:with-param name="count" select="0" />
      </xsl:call-template>
      <xsl:text>;&#010;</xsl:text>

      <xsl:text>     eeprom_instr  = </xsl:text>
      <xsl:call-template name="format-hex">
        <xsl:with-param name="arg" select="$ucEepromInst" />
        <xsl:with-param name="count" select="0" />
      </xsl:call-template>
      <xsl:text>;&#010;</xsl:text>

      </xsl:if> <!-- JTAGICEmkII uses debugWire -->

    </xsl:template>

    <xsl:template name="toupper">
    </xsl:template>

    <!-- return argument $arg if non-empty, 0 otherwise -->
    <xsl:template name="maybezero">
      <xsl:param name="arg" />
      <xsl:choose>
        <xsl:when test="string-length($arg) = 0"><xsl:text>0</xsl:text></xsl:when>
        <xsl:otherwise><xsl:value-of select="$arg" /></xsl:otherwise>
      </xsl:choose>
    </xsl:template> <!-- maybezero -->

    <!-- convert $XX hex number in $arg (if any) into 0xXX; -->
    <!-- return 0 if $arg is empty -->
    <xsl:template name="dollar-to-0x">
      <xsl:param name="arg" />
      <xsl:choose>
        <xsl:when test="string-length($arg) = 0">
          <xsl:text>0</xsl:text>
        </xsl:when>
        <xsl:when test="substring($arg, 1, 1) = '&#036;'">
          <xsl:text>0x</xsl:text>
          <xsl:value-of select="substring($arg, 2)" />
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="$arg" />
        </xsl:otherwise>
      </xsl:choose>
    </xsl:template> <!-- dollar-to-0x -->

    <!-- Format a string of 0xXX numbers: start a new line -->
    <!-- after each 8 hex numbers -->
    <!-- call with parameter $count = 0, calls itself -->
    <!-- recursively then until everything has been done -->
    <xsl:template name="format-hex">
      <xsl:param name="arg" />
      <xsl:param name="count" />
      <xsl:choose>
        <xsl:when test="string-length($arg) &lt;= 4">
          <!-- Last element, print it, and leave template. -->
          <xsl:value-of select="$arg" />
        </xsl:when>
        <xsl:otherwise>
          <!--
            * More arguments follow, print first value,
            * followed by a comma, decide whether a space
            * or a newline needs to be emitted, and recurse
            * with the remaining part of $arg.
          -->
          <xsl:value-of select="substring($arg, 1, 4)" />
          <xsl:choose>
            <xsl:when test="$count mod 8 = 7">
              <xsl:text>,&#010;&#009;             </xsl:text>
            </xsl:when>
            <xsl:otherwise>
              <xsl:text>, </xsl:text>
            </xsl:otherwise>
          </xsl:choose>
          <xsl:variable name="newarg">
            <!-- see whether there is a space after comma -->
            <xsl:choose>
              <xsl:when test="substring($arg, 6, 1) = ' '">
                <xsl:value-of select="substring($arg, 7)" />
              </xsl:when>
              <xsl:otherwise>
                <xsl:value-of select="substring($arg, 6)" />
              </xsl:otherwise>
            </xsl:choose>
          </xsl:variable>
          <xsl:call-template name="format-hex">
            <xsl:with-param name="arg" select="$newarg" />
            <xsl:with-param name="count" select="$count + 1" />
          </xsl:call-template>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:template>

</xsl:stylesheet>
