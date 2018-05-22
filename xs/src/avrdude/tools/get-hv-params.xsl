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
 * Extract high-voltage (parallel and serial) programming parameters
 * out of the Atmel XML files, and convert them into avrdude.conf
 * snippets.
 *
 * Run this file together with the respective AVR's XML file through
 * an XSLT processor (xsltproc, saxon), and capture the output for
 * inclusion into avrdude.conf.in.
-->
    <xsl:output method="text"/>
    <xsl:template match="/">
    <xsl:for-each select="//*">
    <xsl:if test='name() = "STK500_2"'>

    <!--
     * High-voltage parallel programming parameters.
    -->
    <xsl:for-each
     select="*[starts-with(translate(name(),
                                     'abcdefghijklmnopqrstuvwxyz',
                                     'ABCDEFGHIJKLMNOPQRSTUVWXYZ'),
                                     'PP')]">
        <xsl:if test="self::node()[name() = 'PPControlStack']"
        >    pp_controlstack     =
        <xsl:call-template name="format_cstack">
            <xsl:with-param name="stack" select="." />
            <xsl:with-param name="count" select="0" />
        </xsl:call-template>;
</xsl:if> <!-- PPControlStack -->

	<xsl:if test="self::node()[name() = 'PpEnterProgMode']">
            <xsl:for-each select="*">
                <xsl:if test="self::node()[name() = 'stabDelay']"
                >    hventerstabdelay    = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'progModeDelay']"
                >    progmodedelay       = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'latchCycles']"
                >    latchcycles         = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'toggleVtg']"
                >    togglevtg           = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'powerOffDelay']"
                >    poweroffdelay       = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'resetDelayMs']"
                >    resetdelayms        = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'resetDelayUs']"
                >    resetdelayus        = <xsl:value-of select="." />;
</xsl:if>
            </xsl:for-each>
        </xsl:if> <!-- PpEnterProgMode -->

        <xsl:if test="self::node()[name() = 'PpLeaveProgMode']">
            <xsl:for-each select="*">
                <xsl:if test="self::node()[name() = 'stabDelay']"
                >    hvleavestabdelay    = <xsl:value-of select="." />;
</xsl:if>
            </xsl:for-each>
        </xsl:if> <!-- PpLeaveProgMode -->

        <xsl:if test="self::node()[name() = 'PpChipErase']">
            <xsl:for-each select="*">
                <xsl:if test="self::node()[name() = 'pulseWidth']"
                >    chiperasepulsewidth = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'pollTimeout']"
                >    chiperasepolltimeout = <xsl:value-of select="." />;
</xsl:if>
            </xsl:for-each>
        </xsl:if> <!-- PpChipErase -->

        <xsl:if test="self::node()[name() = 'PpProgramFuse']">
            <xsl:for-each select="*">
                <xsl:if test="self::node()[name() = 'pulseWidth']"
                >    programfusepulsewidth = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'pollTimeout']"
                >    programfusepolltimeout = <xsl:value-of select="." />;
</xsl:if>
            </xsl:for-each>
        </xsl:if> <!-- PpProgramFuse -->

        <xsl:if test="self::node()[name() = 'PpProgramLock']">
            <xsl:for-each select="*">
                <xsl:if test="self::node()[name() = 'pulseWidth']"
                >    programlockpulsewidth = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'pollTimeout']"
                >    programlockpolltimeout = <xsl:value-of select="." />;
</xsl:if>
            </xsl:for-each>
        </xsl:if> <!-- PpProgramLock -->

   </xsl:for-each> <!-- PP parameters -->

   <!--
    * High-voltage serial programming parameters.
   -->
   <xsl:for-each
    select="*[starts-with(translate(name(),
                          'abcdefghijklmnopqrstuvwxyz',
                          'ABCDEFGHIJKLMNOPQRSTUVWXYZ'),
                          'HVSP')]">

        <xsl:if test="self::node()[name() = 'HvspControlStack']"
        >    hvsp_controlstack   =
        <xsl:call-template name="format_cstack">
            <xsl:with-param name="stack" select="." />
            <xsl:with-param name="count" select="0" />
        </xsl:call-template>;
</xsl:if> <!-- HvspControlStack -->

        <xsl:if test="self::node()[name() = 'HvspEnterProgMode']">
            <xsl:for-each select="*">
                <xsl:if test="self::node()[name() = 'stabDelay']"
                >    hventerstabdelay    = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'cmdexeDelay']"
                >    hvspcmdexedelay     = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'synchCycles']"
                >    synchcycles         = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'latchCycles']"
                >    latchcycles         = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'toggleVtg']"
                >    togglevtg           = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'powoffDelay']"
                >    poweroffdelay       = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'resetDelay1']"
                >    resetdelayms        = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'resetDelay2']"
                >    resetdelayus        = <xsl:value-of select="." />;
</xsl:if>
            </xsl:for-each>
        </xsl:if> <!-- HvspEnterProgMode -->

        <xsl:if test="self::node()[name() = 'HvspLeaveProgMode']">
            <xsl:for-each select="*">
                <xsl:if test="self::node()[name() = 'stabDelay']"
                >    hvleavestabdelay    = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'resetDelay']"
                >    resetdelay          = <xsl:value-of select="." />;
</xsl:if>
              </xsl:for-each>
        </xsl:if> <!-- HvspLeaveProgMode -->

        <xsl:if test="self::node()[name() = 'HvspChipErase']">
            <xsl:for-each select="*">
                <xsl:if test="self::node()[name() = 'pollTimeout']"
                >    chiperasepolltimeout = <xsl:value-of select="." />;
</xsl:if>
                <xsl:if test="self::node()[name() = 'eraseTime']"
                >    chiperasetime       = <xsl:value-of select="." />;
</xsl:if>
            </xsl:for-each>
        </xsl:if> <!-- HvspChipErase -->

        <xsl:if test="self::node()[name() = 'HvspProgramFuse']">
            <xsl:for-each select="*">
                <xsl:if test="self::node()[name() = 'pollTimeout']"
                >    programfusepolltimeout = <xsl:value-of select="." />;
</xsl:if>
            </xsl:for-each>
        </xsl:if> <!-- HvspProgramFuse -->

        <xsl:if test="self::node()[name() = 'HvspProgramLock']">
            <xsl:for-each select="*">
                <xsl:if test="self::node()[name() = 'pollTimeout']"
                >    programlockpolltimeout = <xsl:value-of select="." />;
</xsl:if>
            </xsl:for-each>
        </xsl:if> <!-- HvspProgramLock -->

    </xsl:for-each> <!-- HVSP parameters -->

    </xsl:if>  <!-- STK500_2 parameters -->
    </xsl:for-each>  <!-- All nodes -->

    </xsl:template>

    <!--
     * Format the control stack argument: replace space-separated
     * list by a list separated with commas, followed by either
     * a space or a newline, dependend on the current argument
     * count.
     * This template calls itself recursively, until the entire
     * argument $stack has been processed.
    -->
    <xsl:template name="format_cstack">
        <xsl:param name="stack" />
        <xsl:param name="count" />
        <xsl:choose>
            <xsl:when test="string-length($stack) &lt;= 4">
                <!-- Last element, print it, and leave template. -->
                <xsl:value-of select="$stack" />
            </xsl:when>
            <xsl:otherwise>
                <!--
                  * More arguments follow, print first value,
                  * followed by a comma, decide whether a space
                  * or a newline needs to be emitted, and recurse
                  * with the remaining part of $stack.
                -->
                <xsl:value-of select="substring($stack, 1, 4)" />
                <xsl:choose>
                    <xsl:when test="$count mod 8 = 7">
                        <!-- comma, newline, 8 spaces indentation -->
                        <xsl:text>,
        </xsl:text>
                    </xsl:when>
                    <xsl:otherwise>
                        <!-- comma, space -->
                        <xsl:text>, </xsl:text>
                    </xsl:otherwise>
                </xsl:choose>
                <xsl:call-template name="format_cstack">
                    <xsl:with-param name="stack" select="substring($stack, 6)"
                    />
                    <xsl:with-param name="count" select="$count + 1" />
                </xsl:call-template>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:template>
</xsl:stylesheet>
