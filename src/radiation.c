
#include "radiation.h"

void get_diffuse_frac(canopy_wk *cw, int doy, double sw_rad) {
    /*
        For the moment, I am only going to implement Spitters, so this is a bit
        of a useless wrapper function.

    */
    spitters(cw, doy, sw_rad);

    return;
}

void spitters(canopy_wk *cw, int doy, double sw_rad) {

    /*
    Spitters algorithm to estimate the diffuse component from the measured
    irradiance.

    Eqn 20a-d.

    Parameters:
    ----------
    doy : int
        day of year
    sw_rad : double
        total incident radiation [J m-2 s-1]

    Returns:
    -------
    diffuse : double
        diffuse component of incoming radiation (returned in cw structure)

    References:
    ----------
    * Spitters, C. J. T., Toussaint, H. A. J. M. and Goudriaan, J. (1986)
      Separating the diffuse and direct component of global radiation and its
      implications for modeling canopy photosynthesis. Part I. Components of
      incoming radiation. Agricultural Forest Meteorol., 38:217-229.
    */
    double So, tau, R, K, diffuse_frac, cos_zen_sq;

    /* sine of the elev of the sun above the horizon is the same as cos_zen */
    So = calc_extra_terrestrial_rad(doy, cw->cos_zenith);

    /* atmospheric transmisivity */
    tau = estimate_clearness(sw_rad, So);

    cos_zen_sq = cw->cos_zenith * cw->cos_zenith;

    /* For zenith angles > 80 degrees, diffuse_frac = 1.0 */
    if (cw->cos_zenith > 0.17) {
        R = 0.847 - 1.61 * cw->cos_zenith + 1.04 * cos_zen_sq;
        K = (1.47 - R) / 1.66;
        if (tau <= 0.22) {
            cw->diffuse_frac = 1.0;
        } else if (tau > 0.22 && tau <= 0.35) {
            cw->diffuse_frac = 1.0 - 6.4 * (tau - 0.22) * (tau - 0.22);
        } else if (tau > 0.35 && tau <= K) {
            cw->diffuse_frac = 1.47 - 1.66 * tau;
        } else {
            cw->diffuse_frac = R;
        }
    } else {
        cw->diffuse_frac = 1.0;
    }

    /* doubt we need this, should check */
    if (cw->diffuse_frac <= 0.0) {
        cw->diffuse_frac = 0.0;
    } else if (cw->diffuse_frac >= 1.0) {
        cw->diffuse_frac = 1.0;
    }

    cw->direct_frac = 1.0 - cw->diffuse_frac;

    return;

}

void calculate_absorbed_radiation(canopy_wk *cw, params *p, state *s,
                                  double par) {
    /*
        Calculate absorded irradiance of sunlit and shaded fractions of
        the canopy. The total irradiance absorbed by the canopy and the
        sunlit/shaded components are all expressed on a ground-area basis!

        NB:  sin_beta == cos_zenith

        References:
        -----------
        * De Pury & Farquhar (1997) PCE, 20, 537-557.

        but see also:
        * Wang and Leuning (1998) AFm, 91, 89-111.
        * Dai et al. (2004) Journal of Climate, 17, 2281-2299.
    */

    int    i;
    double Ib, Id, Is, Ic, scattered, shaded, beam;
    double rho_cd = 0.036;    /* canopy reflection coeffcient for diffuse PAR */
    double rho_cb = 0.029;     /* canopy reflection coeffcient for direct PAR */
    double omega_PAR = 0.15;            /* leaf scattering coefficient of PAR */
    double kb = 0.5 / cw->cos_zenith;   /* beam radiation ext coeff of canopy */
    double k_dash_b = 0.46 / cw->cos_zenith;      /* beam & scat PAR ext coef */
    double k_dash_d = 0.718;     /* diffuse & scattered PAR extinction coeff  */
    double lai = s->lai;
    double lad = p->lad;

    /* Direct-beam irradiance absorbed by sunlit leaves - de P & F, eqn 20b */
    Ib = par * cw->direct_frac;
    beam = Ib * (1.0 - omega_PAR) * (1.0 - exp(-kb * lai));

    /* Diffuse irradiance absorbed by sunlit leaves - de P & F, eqn 20c */
    Id = par * cw->diffuse_frac;
    shaded = (Id * (1.0 - rho_cd) * (1.0 - exp(-(k_dash_d + kb) * lai)) *
              (k_dash_d / (k_dash_d + kb)));

    /* Scattered-beam irradiance abs. by sunlit leaves - de P & F, eqn 20d */
    scattered = (Ib * ((1.0 - rho_cb) * (1.0 - exp(-(k_dash_b + kb) * lai)) *
                        k_dash_b / (k_dash_b + kb) - (1.0 - omega_PAR) *
                        (1.0 - exp(-2.0 * kb * lai)) / 2.0));

    /* Irradiance absorbed by the canopy - de Pury & Farquhar (1997), eqn 13 */
    Ic = ((1.0 - rho_cb) * Ib * (1.0 - exp(-k_dash_b * lai)) +
          (1.0 - rho_cd) * Id * (1.0 - exp(-k_dash_d * lai)));

    /*
    ** Irradiance absorbed by the sunlit fraction of the canopy is the sum of
    ** direct-beam, diffuse and scattered-beam components
    */
    cw->apar_leaf[SUNLIT] = beam + scattered + shaded;

    /*
    **  Irradiance absorbed by the shaded leaf area of the canopy is the
    **  integral of absorbed irradiance in the shade (Eqn A7) and the shdaded
    **  leaf area fraction. Or simply, the difference between the total
    **  irradiacne absorbed by the canopy and the irradiance absorbed by the
    **  sunlit leaf area
    */
    cw->apar_leaf[SHADED] = Ic - cw->apar_leaf[SUNLIT];

    /*
    ** Scale leaf fluxes to the canopy
    ** - The direct radiation on sunlit leaves is assumed to be equal at
    **   all canopy depths but with the fraction of sunlit leaves
    **   decreasing with canopy depth. De Pury & Farquhar 1997, eqn 18.
    */
    cw->lai_leaf[SUNLIT] = (1.0 - exp(-kb * lai)) / kb;
    cw->lai_leaf[SHADED] = lai - cw->lai_leaf[SUNLIT];

    /* convert apar from unit ground to leaf area */
    /*cw->apar_leaf[SUNLIT] /= cw->lai_leaf[SUNLIT];
    cw->apar_leaf[SHADED] /= cw->lai_leaf[SHADED];*/

    return;
}

void calculate_solar_geometry(canopy_wk *cw, params *p, double doy,
                              double hod) {

    /*
        The solar zenith angle is the angle between the zenith and the centre
        of the sun's disc. The solar elevation angle is the altitude of the
        sun, the angle between the horizon and the centre of the sun's disc.
        Since these two angles are complementary, the cosine of either one of
        them equals the sine of the other, i.e. cos theta = sin beta. I will
        use cos_zen throughout code for simplicity.

        Arguments:
        ----------
        params : p
            params structure
        doy : double
            day of year
        hod : double:
            hour of the day [0.5 to 24]
        cos_zen : double
            cosine of the zenith angle of the sun in degrees (returned)
        elevation : double
            solar elevation (degrees) (returned)

        References:
        -----------
        * De Pury & Farquhar (1997) PCE, 20, 537-557.

    */

    double dec, et, lc, t0, h, gamma, zenith_angle, hour_angle, rlat, sin_beta;

    /* need to convert 30 min data, 0-47 to 0-23.5 */
    hod /= 2.0;

    gamma = day_angle(doy);
    dec = calculate_solar_declination(doy, gamma);
    et = calculate_eqn_of_time(gamma);
    t0 = calculate_solar_noon(et, p->longitude);
    h = calculate_hour_angle(hod, t0);
    rlat = DEG2RAD(p->latitude);

    /* A13 - De Pury & Farquhar */
    sin_beta = sin(rlat) * sin(dec) + cos(rlat) * cos(dec) * cos(h);
    cw->cos_zenith = sin_beta; /* The same thing, going to use throughout */
    if (cw->cos_zenith > 1.0)
        cw->cos_zenith = 1.0;
    else if (cw->cos_zenith < 0.0)
        cw->cos_zenith = 0.0;

    zenith_angle = RAD2DEG(acos(cw->cos_zenith));
    cw->elevation = 90.0 - zenith_angle;

    return;
}

double calculate_solar_noon(double et, double longitude) {
    /* Calculation solar noon - De Pury & Farquhar, '97: eqn A16

    Reference:
    ----------
    * De Pury & Farquhar (1997) PCE, 20, 537-557.

    Returns:
    ---------
    t0 - solar noon (hours).
    */
    double t0, Ls;

    /* all international standard meridians are multiples of 15deg east/west of
       greenwich */
    Ls = round_to_value(longitude, 15.);
    t0 = 12.0 + (4.0 * (Ls - longitude) - et) / 60.0;

    return (t0);
}

double calculate_hour_angle(double t, double t0) {
    /* Calculation solar noon - De Pury & Farquhar, '97: eqn A15

    Reference:
    ----------
    * De Pury & Farquhar (1997) PCE, 20, 537-557.

    Returns:
    ---------
    h - hour angle (radians).
    */
    return (M_PI * (t - t0) / 12.0);

}

double day_angle(int doy) {
    /* Calculation of day angle - De Pury & Farquhar, '97: eqn A18

    Reference:
    ----------
    * De Pury & Farquhar (1997) PCE, 20, 537-557.
    * J. W. Spencer (1971). Fourier series representation of the position of
      the sun.

    Returns:
    ---------
    gamma - day angle in radians.
    */

    return (2.0 * M_PI * (doy - 1.0) / 365.0);
}

double calculate_solar_declination(int doy, double gamma) {
    /*
    Solar Declination Angle is a function of day of year and is indepenent
    of location, varying between 23deg45' to -23deg45'

    Arguments:
    ----------
    doy : int
        day of year, 1=jan 1
    gamma : double
        fractional year (radians)

    Returns:
    --------
    dec: float
        Solar Declination Angle [radians]

    Reference:
    ----------
    * De Pury & Farquhar (1997) PCE, 20, 537-557.
    * Leuning et al (1995) Plant, Cell and Environment, 18, 1183-1200.
    * J. W. Spencer (1971). Fourier series representation of the position of
      the sun.
    */
    double decl;

    /* declination (radians) */
    /*decl = 0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma) - \
           0.006758 * cos(2.0 * gamma) + 0.000907 * sin(2.0 * gamma) -\
           0.002697 * cos(3.0 * gamma) + 0.00148 * sin(3.0 * gamma);*/


    /* (radians) A14 - De Pury & Farquhar  */
    decl = -23.4 * (M_PI / 180.) * cos(2.0 * M_PI * (doy + 10) / 365);

    return (decl);

}

double calculate_eqn_of_time(double gamma) {
    /* Equation of time - correction for the difference btw solar time
    and the clock time.

    Arguments:
    ----------
    doy : int
        day of year
    gamma : double
        fractional year (radians)

    References:
    -----------
    * De Pury & Farquhar (1997) PCE, 20, 537-557.
    * Campbell, G. S. and Norman, J. M. (1998) Introduction to environmental
      biophysics. Pg 169.
    * J. W. Spencer (1971). Fourier series representation of the position of
      the sun.
    * Hughes, David W.; Yallop, B. D.; Hohenkerk, C. Y. (1989),
      "The Equation of Time", Monthly Notices of the Royal Astronomical
      Society 238: 1529–1535
    */
    double et, f, A;

    /* radians */
    /*et = 0.000075 + 0.001868 * cos(gamma) - 0.032077 * sin(gamma) -\
         0.014615 * cos(2.0 * gamma) - 0.04089 * sin(2.0 * gamma);*/

    /* radians to minutes */
    /*et *= 229.18; */

    /* radians to hours */
    /*et *= 24.0 / (2.0 * M_PI);*/

    /* minutes - de Pury and Farquhar, 1997 - A17 */
    et = (0.017 + 0.4281 * cos(gamma) - 7.351 * sin(gamma) - 3.349 *
          cos(2.0 * gamma) - 9.731  * sin(gamma));

    return (et);
}



double calc_extra_terrestrial_rad(double doy, double cos_zenith) {
    /* Solar radiation incident outside the earth's atmosphere, e.g.
    extra-terrestrial radiation. The value varies a little with the earths
    orbit.

    Using formula from Spitters not Leuning!

    Arguments:
    ----------
    doy : double
        day of year
    cos_zenith : double
        cosine of zenith angle (radians)

    Returns:
    --------
    So : float
        solar radiation normal to the sun's bean outside the Earth's atmosphere
        (J m-2 s-1)

    Reference:
    ----------
    * Spitters et al. (1986) AFM, 38, 217-229, equation 1.
    */

    double So, Sc;

    /* Solar constant (J m-2 s-1) */
    Sc = 1370.0;

    if (cos_zenith > 0.0) {
        /*
        ** remember sin_beta = cos_zenith; trig funcs are cofuncs of each other
        ** sin(x) = cos(90-x) and cos(x) = sin(90-x).
        */
        So = Sc * (1.0 + 0.033 * cos(doy / 365.0 * 2.0 * M_PI)) * cos_zenith;
    } else {
        So = 0.0;
    }

    return (So);

}


double estimate_clearness(double sw_rad, double So) {
    /*
        estimate atmospheric transmisivity - the amount of diffuse radiation
        is a function of the amount of haze and/or clouds in the sky. Estimate
        a proxy for this, i.e. the ratio between global solar radiation on a
        horizontal surface at the ground and the extraterrestrial solar
        radiation
    */

    /* Fraction of global radiation that is PAR */
    double fpar = 0.5;
    double tau;

    /* catch possible divide by zero when zenith = 90. */
    if (So <= 0.0)
        tau = 0.0;
    else
        tau = sw_rad * fpar / So ;

    if (tau > 1.0) {
        tau = 1.0;
    } else if (tau < 0.0) {
        tau = 0.0;
    }

    return (tau);
}
